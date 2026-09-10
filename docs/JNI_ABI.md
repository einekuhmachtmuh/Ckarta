# Ckarta JNI ABI 基線

本文件固定 JNI（Java Native Interface，Java 原生介面）邊界；完整成本研究見 docs/JNI_COST_MODEL.md，thread ownership 見 docs/THREAD_MODEL.md。

## 1. 定位

JNI ABI（應用程式二進位介面）是 C 資料平面與 Java Servlet 容器之間的最小邊界。第一階段禁止建立萬用 JNI API。

## 2. Invocation

正式產品由 C main() 啟動 JVM。

Oracle JNI Invocation API：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html

主要 API：JNI_CreateJavaVM、DestroyJavaVM、AttachCurrentThread、DetachCurrentThread、GetEnv。

## 3. Descriptor 與生命週期

第一階段採 process-local request descriptor；Java 不直接接收 descriptor struct 或 native pointer。

固定欄位：
- `abi_version`：ABI 版本。
- `struct_size`：實際大小，用於版本／布局檢查。
- `feature_flags`：功能旗標。
- `ownership_flags`：owner／borrow 語意。
- `owner_token`：C owner 的邏輯識別。
- `lifetime_token`：native storage 有效期間的識別。
- `request_id`：request 邏輯識別。
- `metadata`／`metadata_length`：C-owned compact canonical request metadata view；目前由 method、target、protocol 三個 big-endian 32-bit length 加上 bytes 與一個 connection-close flag 組成，Java 一次取得 DirectByteBuffer 後以 slice view 存取，不逐欄建立 header object。
- `body`／`body_length`：C-owned native bytes；`body_length == 0` 時 `body` 可為 NULL；非零長度仍須 `body != NULL` 且 `body_length <= INT32_MAX`，以符合目前 DirectByteBuffer boundary。

descriptor 不是 wire protocol；atomic lifecycle state 不放入 descriptor，避免把同步實作細節固定成 ABI。

### 狀態機

`PENDING → RUNNING → COMPLETED|FAILED`；失敗由 `RUNNING → FAILING → FAILED` 發布。取消可由 `PENDING|RUNNING → CANCELLING`。一旦進入 `CANCELLING`，不得再被 `COMPLETED`、`FAILING` 或 `FAILED` 覆寫。`FAILING` 是 private publication state，不代表對外 terminal outcome。

C 實作用 atomic CAS 保證競爭 cancellation 不重複取得 terminal ownership；failure 則先 CAS 至 private `FAILING` publication state，再寫 error record，最後 release-store `FAILED`。

## 4. Canonical HTTP request handoff

目前 executable handoff 為：HTTP parser → `ck_request_init_http()` → canonical request descriptor → JNI `dispatchAsync()` → Java `NativeRequest` facade。`ck_request_init_http()` 只複製 method/target/protocol 的 bounded metadata，不複製 header object graph；body 仍由呼叫者保證在 Java borrow lifetime 內有效。

metadata wire view 格式固定為：`u32 method_length | u32 target_length | u32 protocol_length | method bytes | target bytes | protocol bytes | u8 connection_close_required`；u32 使用 network/big-endian order。Java 不取得 `ck_http_request_t` 或其 span pointer。

目前 handoff slice 的 Java runtime 只驗證 method=GET、protocol=HTTP/1.1、target 非空且沒有 `Connection: close`，並將 body/metadata 作 read-only DirectByteBuffer view。這是 canonical request handoff smoke contract，不是完整 `HttpServletRequest` 實作。

## 5. C struct → Java object

禁止把 C request struct（請求結構）逐欄映射為大量 Java fields、Strings 或 header objects。

初步核准模型：

C canonical request
→ opaque request handle
→ 一個 Java request facade（請求外觀）
→ 一次批次初始化
→ DirectByteBuffer data view（直接位元組緩衝區資料視圖）

OpenJDK 21 HotSpot 的 NewObjectA／NewObjectV 涉及 Java instance allocation、local JNI handle、參數整理與 constructor invocation；大量 Set/Get field 因此不是單純記憶體映射。

## 6. Buffer ownership

Java 使用 native buffer 時預設為 borrow-only（借用）；Java 不得 free。C 不得在 Java borrow 未結束前 recycle。

NewDirectByteBuffer 可提供 native memory view，但不決定 Ckarta ownership；native allocation lifetime 必須覆蓋所有 Java 使用時間。

## 7. Thread rules

`JNIEnv*` 不得跨執行緒共享。C event-loop thread 不得執行 Servlet application code。

第一階段的可實測邊界包括：C worker attached submission、worker-group JNI bridge、central JNI bridge pool；其中 attached worker 只能執行 JNI control／submission，不得直接執行 Servlet application。

JNI bridge thread 必須具有自己的 attachment／detach 生命週期。需要保存的是 process-level `JavaVM*`，不是可跨 thread 傳遞的 `JNIEnv*`。

具體 thread topology 見 docs/THREAD_MODEL.md。

## 8. Exception

每次可能建立 pending Java exception 的 JNI operation 都必須依 JDK 版本規格在適當邊界檢查；不得無條件清除 pending exception，也不得把 Throwable 的私有實作細節變成 C ABI。

完整 exception taxonomy、translation、cleanup、security disclosure 與 async error contract 見 `docs/EXCEPTION_HANDLING_RESEARCH.md`。

## 9. Async

JNI 呼叫返回不等於 request 完成。Servlet AsyncContext 可以讓請求在 Java method return 後繼續存在。

## 10. ABI stability

正式 ABI 穩定前必須有：version、struct size、feature flags（功能旗標）、reserved fields（保留欄位）、ownership flags（所有權旗標）。

不得依賴 C struct 自然布局作為長期 ABI，除非另有明確相容性契約。

## 11. Forbidden

禁止：expose raw socket fd to Servlet application、expose C pool pointer、Java free native memory、C access private Java object internals、hidden global native state。

## 12. JNI crossing 策略

優先：read buffer → parse → canonical descriptor → bounded semantic handoff → Java executor → Java processing → completion record。

semantic handoff 可由 attached submission 或受控 bridge queue 實作；實際 topology 必須由 benchmark 決定。

避免每個 header、body chunk 或 write operation 都跨 JNI。

## 13. 成本與 API 選擇

Call<Type>MethodA/V：粗粒度 request／stage dispatch。

NewObjectA/V：只建立必要薄 facade，不逐欄建立 mirror object（鏡像物件）。

Set/Get field：只作少量狀態。

String／array：優先 lazy materialization（延遲物件化）；不可假設所有 array access 都零拷貝。

GetPrimitiveArrayCritical：只可作符合 JNI critical region 限制的短操作。

NewDirectByteBuffer／GetDirectBufferAddress：優先作大量 native bytes 視圖，但 ownership／lifetime 必須由 Ckarta 明確管理。

## 14. 學術與 ownership 背景

Clarke、Potter、Noble 的 *Ownership Types for Flexible Alias Protection* 將 ownership 與 alias visibility／representation containment 形式化；Ckarta 只借用其 ownership 設計思想，correctness 仍由 C lifecycle、JNI specification 與測試決定。

來源：https://doi.org/10.1145/286936.286947

## 15. Error record boundary

`c/error/ck_error.[ch]` 已提供 process-local structured error record 與 layout/validation test。`ck_request_t` 目前已內含此 record；failure publication 使用 `RUNNING → FAILING` CAS，由唯一勝出者寫入 error，再以 release-store 發布 `FAILED`；讀者在 acquire-load 看到 `FAILED` 後才可取得 error record。它不是 Java Throwable ABI，也不是 runtime-loadable module ABI。

完整 state matrix 見 `docs/ERROR_STATE_MATRIX.md`；Ckarta 實際函式流程見 `docs/CKARTA_FUNCTION_FLOW.md`。

## 16. 研究與 benchmark

OpenJDK 21 成本基線與 API 比較見 docs/JNI_COST_MODEL.md；fixed-tag HotSpot audit 見 docs/OPENJDK_21U_SOURCE_AUDIT.md。

thread model 的 direct-attach 與 JNI bridge 差異見 docs/THREAD_MODEL.md。

該文件把 OpenJDK 21 原始碼分析、歷史 JNI benchmark 與本機 sanity test 分開；未完成 Ckarta 自有 benchmark 前，不得宣稱某 JNI API 或 thread topology 更快。

## 17. Executor handoff slice

目前 executable path 使用 runtime-owned bounded native completion queue：Java executor thread 完成 request 後透過 `RegisterNatives` 綁定的 `publishCompletion(long, long, long, long, long, int)` 直接發布 value-only record。Native callback 不保存 `JNIEnv*`、Java Throwable 或 Java object graph，只將固定整數欄位寫入 native queue。

Java executor 必須使用有界工作佇列；native completion queue 另有獨立有界容量與 close-aware producer backpressure。Java producer 在 queue 滿時可阻塞等待 native consumer 釋放容量，但 C event loop 本身不得因此阻塞。

目前 executable slice 使用 Java ThreadPoolExecutor 的有界工作佇列。C worker 僅負責 submission 後 detach；Java executor thread 建立 NativeRequest 並執行 smoke workload；完成後由 JNI publisher 將 value-only completion record 寫入 native bounded completion queue。

Java executor 必須使用有界容量；飽和時不得 fallback 到 C event-loop thread 執行 Servlet application。

目前已固定 Jakarta Servlet API dependency `jakarta.servlet:jakarta.servlet-api:6.1.0` 作為 application-facing API compile/test boundary；`CkartaServletAsyncContext` 是薄 adapter，不把 native connection、queue 或 token 暴露給 Servlet application。這不是 TCK compatibility claim。

## 18. Native completion notification slice

C runtime 另有 Linux `eventfd(EFD_CLOEXEC | EFD_NONBLOCK)` notification primitive：`c/event/ck_completion_notification.[ch]` 可建立 notification fd、coalescing signal 與 drain。這個 primitive 已被接入目前單一 executable smoke path：`c/core/main.c` 以 `ck_runtime_completion_fd()` 將 notification fd 加入 `ck_event_loop`，等待 `ck_event_loop_wait()` 返回後 drain notification，再由 bounded completion queue routing 取得 completion record。

因此目前正確狀態是：**Linux eventfd → Ckarta event-backend wait 的單一 executable integration 已存在；尚未完成 production multi-worker/cross-platform final backend。** 不應再將它描述為 polling-only，也不得把這個 smoke integration 擴大宣稱為正式多 worker completion architecture。

多請求 completion 的 value-only record 固定包含 request_id、owner_token、lifetime_token、cycle_id、result 與 status。Java side 不持有 completion queue ownership，也不接收 raw native object graph。

Consumer 必須先 drain notification，再反覆 dequeue 至 queue 為空；notification 本身只是 wake-up mechanism，不是 completion record storage。queue overflow、shutdown drain、cancellation、duplicate completion、late completion 與 owner disappearance 的完整 production semantics 仍由後續正式 completion routing contract 定義。

## 19. 多請求 completion ownership

目前 completion record 已包含 request_id、owner_token、lifetime_token、cycle_id、terminal status；native queue 只保存 value-only record。這是 executable routing slice，不是 public plugin ABI。

正式多 worker completion routing 仍必須定義 owner worker selection、shutdown drain、cancellation、duplicate/late completion 與 owner teardown 的完整 lifecycle contract；不得因目前單一 executable notification path 已存在，就把這些未完成事項視為已解決。

## 20. Container-internal native async capability

native connection registry 現提供固定容量 process-local ownership table。Java side 僅可透過 package-private `CkartaNativeAsyncBridge` 持有兩個 opaque `long` capability values：registry capability 與 generation-protected connection handle；Java 不能解參照、運算或轉型為 native pointer，也不屬 application-facing Servlet ABI。

JNI native methods：

- `nativeStartAsyncCycle(long,long,long,long,long,long): int`
- `nativeTryTerminal(long,long,long,long,long,long,int): int`

C side 先驗證 capability、request_id、owner_token、lifetime_token、cycle_id 與 event range，再進入 registry；registry lookup、identity validation 與 connection state transition 皆受明確 ownership 規則約束。

terminal return contract：0=CLAIMED；1=ALREADY_SAME；2=ALREADY_DIFFERENT；負值=bridge/identity/state error。Java semantic core 將 1 視為可安全反映的同事件 delayed notification，將 2 視為已由另一 terminal winner 取得 ownership，將負值視為 bridge error；application-facing `complete()` 因此不會覆寫 native winner。

這個 capability 目前是 container-internal testable boundary，不是 public plugin ABI；正式 container 注入與 lifecycle teardown 尚待實作。
