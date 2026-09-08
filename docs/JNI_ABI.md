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
- `abi_version)：ABI 版本。
- `struct_size)：實際大小，用於版本／布局檢查。
- `feature_flags)：功能旗標。
- `ownership_flags)：owner／borrow 語意。
- `owner_token)：C owner 的邏輯識別。
- `lifetime_token)：native storage 有效期間的識別。
- `request_id)：request 邏輯識別。
- `body`／`body_length)：C-owned native bytes。

descriptor 不是 wire protocol；atomic lifecycle state 不放入 descriptor，避免把同步實作細節固定成 ABI。

### 狀態機

`PENDING → RUNNING → COMPLETED|FAILED`；取消可由 `PENDING|RUNNING → CANCELLING`。一旦進入 `CANCELLING`，不得再被 `COMPLETED` 或 `FAILED` 覆寫。

C 實作用 atomic CAS 保證競爭 cancellation 不重複取得 terminal ownership。

## 4. C struct → Java object

禁止把 C request struct（請求結構）逐欄映射為大量 Java fields、Strings 或 header objects。

初步核准模型：

C canonical request
→ opaque request handle
→ 一個 Java request facade（請求外觀）
→ 一次批次初始化
→ DirectByteBuffer data view（直接位元組緩衝區資料視圖）

OpenJDK 21 HotSpot 的 NewObjectA／NewObjectV 涉及 Java instance allocation、local JNI handle、參數整理與 constructor invocation；大量 Set/Get field 因此不是單純記憶體映射。

## 5. Buffer ownership

Java 使用 native buffer 時預設為 borrow-only（借用）；Java 不得 free。C 不得在 Java borrow 未結束前 recycle。

NewDirectByteBuffer 可提供 native memory view，但不決定 Ckarta ownership；native allocation lifetime 必須覆蓋所有 Java 使用時間。

## 6. Thread rules

`JNIEnv*` 不得跨執行緒共享。第一階段不把所有 C worker 固定 attach JVM；C worker 以 request descriptor 經 bounded JNI queue 交由受控 JNI bridge thread／pool 執行跨 JVM dispatch。

JNI bridge thread 必須具有自己的 attachment／detach 生命週期。需要保存的是 process-level `JavaVM*`，不是可跨 thread 傳遞的 `JNIEnv*`。

具體 thread topology（執行緒拓撲）與 direct-attach alternative（直接附加替代方案）見 docs/THREAD_MODEL.md。

## 7. Exception

每次 JNI 呼叫後都必須檢查 exception。C 不得依賴 Java exception object 的私有實作細節。

## 8. Async

JNI 呼叫返回不等於 request 完成。Servlet AsyncContext 可以讓請求在 Java method return 後繼續存在。

## 9. ABI stability

正式 ABI 穩定前必須有：version、struct size、feature flags（功能旗標）、reserved fields（保留欄位）、ownership flags（所有權旗標）。

不得依賴 C struct 自然布局作為長期 ABI，除非另有明確相容性契約。

## 10. Forbidden

禁止：expose raw socket fd to Servlet application、expose C pool pointer、Java free native memory、C access private Java object internals、hidden global native state。

## 11. JNI crossing 策略

優先：read buffer → parse → canonical descriptor → bounded JNI queue → JNI bridge transition → Java processing → completion queue。

避免每個 header、body chunk 或 write operation 都跨 JNI。

## 12. 成本與 API 選擇

Call<Type>MethodA/V：粗粒度 request／stage dispatch。

NewObjectA/V：只建立必要薄 facade，不逐欄建立 mirror object（鏡像物件）。

Set/Get field：只作少量狀態。

String／array：優先 lazy materialization（延遲物件化）；不可假設所有 array access 都零拷貝。

GetPrimitiveArrayCritical：只可作符合 JNI critical region 限制的短操作。

NewDirectByteBuffer／GetDirectBufferAddress：優先作大量 native bytes 視圖，但 ownership／lifetime 必須由 Ckarta 明確管理。

## 13. 學術與 ownership 背景

Clarke、Potter、Noble 的 *Ownership Types for Flexible Alias Protection* 將 ownership 與 alias visibility／representation containment 形式化；Ckarta 只借用其 ownership 設計思想，correctness 仍由 C lifecycle、JNI specification 與測試決定。

來源：https://doi.org/10.1145/286936.286947

## 14. 研究與 benchmark

OpenJDK 21 成本基線與 API 比較見 docs/JNI_COST_MODEL.md；fixed-tag HotSpot audit 見 docs/OPENJDK_21U_SOURCE_AUDIT.md。

thread model 的 direct-attach 與 JNI bridge 差異見 docs/THREAD_MODEL.md。

該文件把 OpenJDK 21 原始碼分析、歷史 JNI benchmark 與本機 sanity test 分開；未完成 Ckarta 自有 benchmark 前，不得宣稱某 JNI API 或 thread topology 更快。
