# Ckarta Connection Lifecycle 與 Ownership 基線

## 1. 目的

本文件固定 C connection、request、Java Servlet request 與 AsyncContext 的生命週期關係；Servlet 6.1 API 與固定 Tomcat 11.0.25 source 是語意 authority，C implementation 只能在不改變 application-visible semantics 的前提下實作。

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

目前 executable native connection slice 定義：

```text
OPEN
  ↓ startAsync
ASYNC_WAIT
  ↓ terminal CAS
CLOSING
  ↓ close
CLOSED
```

`ck_connection_try_terminal()` 將 terminal reason 與 `CLOSING` state 放在同一 atomic 64-bit lifecycle word 中一次發布，避免 state 與 reason 分兩次寫入造成 publication race。

terminal reason：

- `COMPLETE`
- `CLIENT_DISCONNECT`
- `TIMEOUT`
- `ERROR`
- `SHUTDOWN`

第一次合法 terminal CAS 的 caller 取得唯一 terminal ownership；後續 terminal event 回傳 non-owner result，不能改寫 reason 或重複 cleanup。

## 4. C Request

Request owner 是 connection，直到 request 被明確移交至 asynchronous owner。`owner_token` 與 `lifetime_token` 是驗證用識別，不取代實際 owner；native storage 的回收責任仍屬 owner。

Request 包含：

- request pool
- parser state
- normalized request metadata
- body state
- response state
- JNI handle
- completion state

## 5. Java Request

Java request object 是 Servlet API facade。

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
→ DirectByteBuffer data view

避免：

C struct
→ N 個 Set(Field)
→ M 個 Java String
→ K 個 header object

OpenJDK 21 HotSpot 的 `NewObjectA`／`NewObjectV` 會配置 Java instance、建立 local JNI handle 並呼叫 constructor；大量欄位 JNI 存取因此不是直接記憶體映射。

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp

## 7. Buffer ownership

所有 buffer 都必須標示：

OWNER
BORROWER
LIFETIME
READ/WRITE PERMISSION

Java 使用 native buffer 時，預設為 borrow-only。

Java 不得 free native buffer。

C 不得在 Java 尚未完成 borrow 時 recycle buffer。

`NewDirectByteBuffer` 可建立指向 native memory 的 Java ByteBuffer，但不會替 Ckarta 定義 ownership；DirectByteBuffer reference lifetime 必須與 native allocation lifetime 綁定。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

## 8. AsyncContext

當 Servlet 呼叫 `startAsync()`：

connection lifetime
與
Servlet invocation lifetime

必須分離。

不得在 Java method return 後立即銷毀 C request state。

預計 native correlation：

```text
Servlet service()
        ↓ startAsync()
connection OPEN → ASYNC_WAIT
        ↓
AsyncContext.complete / error / timeout
        ↓
native terminal candidate
        ↓
CLOSING → CLOSED
```

這是 bridge contract，不代表目前已有完整 Servlet AsyncContext implementation。

## 9. Cancellation

以下事件都必須能提出 cancellation/terminal candidate：

- client disconnect
- timeout
- Servlet async complete
- Servlet async timeout
- worker shutdown
- upstream failure

最後 terminal state 與資源回收責任只能由單一 owner 決定。

同一 operation 不能因兩個競爭 path double free；terminal transition 必須由單一 atomic ownership decision 決定。

## 10. Error propagation

C → Java：native error code + stable metadata

Java → C：Servlet exception／response state／completion state

不得讓 C 直接解讀 Java exception object 的 private implementation detail。

## 11. JNI crossing

JNI entry point 應以「一次完成一個有意義的工作單元」為原則。

推薦：

read buffer
→ parse
→ canonical descriptor
→ single JNI transition
→ Java processing
→ native completion publication

避免：

read 64 bytes
→ JNI
→ Java
→ JNI
→ read 64 bytes

## 12. Thread rules

JNIEnv pointer 不得跨執行緒共享。

每個需要使用 JNI 的 native thread 必須具有自己的 JNI attachment 狀態，並在生命週期終止時按 JNI 規則 detach。

目前 Java completion producer 可在 executor thread 透過 registered JNI method 發布 value-only completion；native queue 不保存 `JNIEnv*`、Java Throwable 或 Java object reference。

## 13. 暫時禁止

在下列文件尚未完成並審查前，不建立正式 public JNI API：

- docs/HTTP_FRAMING_POLICY.md
- docs/CANCELLATION_MODEL.md
- docs/JNI_ABI.md
- docs/CONCURRENCY_MODEL.md

目前 `publishCompletion` 屬於 runtime-internal registration，用於 executable smoke/integration slice，不應視為穩定 public module ABI。

## 14. Executable lifecycle slice

`c/jni/ck_request.[ch]` 將 descriptor 與 atomic lifecycle state 分離。同步 smoke path 的 descriptor/body 由 caller 擁有，worker 只借用；caller 在 `pthread_join()` 後才離開其生命週期。

`c/connection/ck_connection.[ch]` 現在額外提供 native connection ownership state machine 與 token validation；它仍未管理真正 socket、TLS 或 Servlet request object。

## 15. AsyncContext bridge 尚待整合

Tomcat 11.0.25 `AsyncContextImpl` 顯示真正 async lifecycle 還包含 `start()`、`complete()`、`timeout()`、`onError()`、`onComplete()`、recycle 與 concurrent-use protection；其中 recycle 必須先建立不可再用的狀態標記，再清除 request/context 等欄位。Ckarta 因此不得只把 `startAsync()` 當成「把 request 放進背景 thread」。

正式 bridge 尚需：

1. Java AsyncContext reference lifetime。
2. native connection owner handoff。
3. timeout/error/client-disconnect precedence。
4. response/output ownership。
5. cross-thread cancellation。
6. post-recycle invalidation。
7. shutdown drain。
8. Servlet 6.1 TCK compatibility tests。

固定 Tomcat source：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/AsyncContextImpl.java

學術 correctness 基線：Herlihy/Wing 的 linearizability。來源：https://doi.org/10.1145/78969.78972
