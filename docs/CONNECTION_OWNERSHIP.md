# Ckarta Connection Lifecycle 與 Ownership 基線

## 1. 目的

本文件在建立 JNI API 前，先固定 C connection、request、Java Servlet request 與 AsyncContext 的生命週期關係。

## 2. 生命周期

推薦：

SERVER
↓
WORKER
↓
CONNECTION
↓
REQUEST
↓
SUBREQUEST

Java asynchronous operation 可以超出同步 Servlet service invocation，但不能超出其合法 container lifecycle。

## 3. C Connection

C connection object 擁有：

- socket descriptor
- TLS state
- event registration state
- connection timer references
- input/output buffer references
- current request reference
- lifecycle state

Connection owner 必須是唯一 authority。

## 4. C Request

Request owner 是 connection，直到 request 被明確移交至 asynchronous owner。\n\n`owner_token` 與 `lifetime_token` 是驗證用識別，不取代實際 owner；native storage 的回收責任仍屬 owner。

Request 包含：

- request pool
- parser state
- normalized request metadata
- body state
- response state
- JNI handle
- completion state

## 5. Java Request

Java request object 是 Servlet API facade（Servlet API 外觀）。

它不得取得 C connection 的直接 ownership。

它只能透過受控 JNI bridge 使用：

- request metadata
- body access
- response sink
- async lifecycle operation

## 6. JNI 物件成本與所有權

Java request facade 不得是 C struct 的逐欄鏡像。

核准的初步模型：

C canonical request
→ opaque request handle
→ 一個 Java facade
→ 一次批次初始化
→ DirectByteBuffer data view（直接位元組緩衝區資料視圖）

避免：

C struct
→ N 個 Set<Field>
→ M 個 Java String
→ K 個 header object

OpenJDK 21 HotSpot 的 `NewObjectA`／`NewObjectV` 會配置 Java instance、建立 local JNI handle 並呼叫 constructor；大量欄位 JNI 存取因此不是「把記憶體直接映射進 Java object」。

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp

## 7. Buffer ownership

所有 buffer 都必須標示：

OWNER
BORROWER
LIFETIME
READ/WRITE PERMISSION

Java 使用 native buffer 時，預設為 borrow-only（借用）。

Java 不得 free native buffer。

C 不得在 Java 尚未完成 borrow 時 recycle buffer。

`NewDirectByteBuffer` 可建立指向 native memory 的 Java ByteBuffer，但不會替 Ckarta 定義 ownership；因此 DirectByteBuffer reference lifetime（直接緩衝區參照生命週期）必須與 native allocation lifetime（原生配置生命週期）明確綁定。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

## 8. AsyncContext

當 Servlet 呼叫 startAsync：

Ckarta 必須把：

connection lifetime
與
Servlet invocation lifetime

分離。

不得在 Java method return 後立即銷毀 C request state。

應轉換：

SERVLET_ACTIVE
→ ASYNC_WAIT

當 complete／timeout／error：

ASYNC_WAIT
→ COMPLETING
→ OUTPUT／CLOSE

## 9. Cancellation

以下事件都必須能取消 pending operation（待處理操作）：

- client disconnect
- timeout
- Servlet async complete
- Servlet async timeout
- worker shutdown
- upstream failure

取消必須具有 idempotent（冪等）語意。

同一 operation 不能因兩個競爭 cancellation path 而 double free；terminal transition 必須只由 owner 成功取得。

## 10. Error propagation

C → Java：
native error code + stable metadata

Java → C：
Servlet exception／response state／completion state

不得讓 C 直接解讀 Java exception object 的內部 implementation detail（實作細節）。

## 11. JNI crossing

JNI entry point 應以「一次完成一個有意義的工作單元」為原則。

推薦：

read buffer
→ parse
→ canonical descriptor
→ single JNI transition
→ Java processing

避免：

read 64 bytes
→ JNI
→ Java
→ JNI
→ read 64 bytes

## 12. Thread rules

JNIEnv pointer（JNI 環境指標）不得跨執行緒共享。

每個需要使用 JNI 的 native thread 必須具有自己的 JNI attachment 狀態，並在生命週期終止時按 JNI 規則 detach。

## 13. 暫時禁止

在下列文件尚未完成並審查前，不建立正式 JNI public API：

- docs/HTTP_FRAMING_POLICY.md
- docs/CANCELLATION_MODEL.md
- docs/JNI_ABI.md
- docs/CONCURRENCY_MODEL.md

成本模型見：

docs/JNI_COST_MODEL.md

## 14. Executable lifecycle slice

目前 `c/jni/ck_request.[ch]` 將 descriptor 與 atomic lifecycle state 分離。smoke path 的 descriptor 與 body 由 caller 擁有，worker 只借用；caller 在 `pthread_join()` 後才離開其生命週期，因此 worker 不會在 owner 已失效後讀取。此模式只適用同步 smoke path；正式 async request 必須使用明確長生命週期 owner。
