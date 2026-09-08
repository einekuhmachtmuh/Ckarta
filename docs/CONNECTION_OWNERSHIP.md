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

Request owner 是 connection，直到 request 被明確移交至 asynchronous owner。

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

## 6. AsyncContext

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

## 7. Buffer ownership

所有 buffer 都必須標示：

OWNER
BORROWER
LIFETIME
READ/WRITE PERMISSION

Java 使用 native buffer 時，預設為 borrow-only（借用）。

Java 不得 free native buffer。

C 不得在 Java 尚未完成 borrow 時 recycle buffer。

## 8. Cancellation

以下事件都必須能取消 pending operation（待處理操作）：

- client disconnect
- timeout
- Servlet async complete
- Servlet async timeout
- worker shutdown
- upstream failure

取消必須具有 idempotent（冪等）語意。

同一 operation 不能因兩個競爭 cancellation path 而 double free。

## 9. Error propagation

C → Java：

native error code + stable metadata

Java → C：

Servlet exception／response state／completion state

不得讓 C 直接解讀 Java exception object 的內部 implementation detail（實作細節）。

## 10. JNI 原則

JNI entry point 應以「一次完成一個有意義的工作單元」為原則。

避免：

read 64 bytes → JNI → Java → JNI → read 64 bytes

優先：

read buffer
→ parse
→ descriptor
→ single JNI transition
→ Java request processing

## 11. 暫時禁止

在下列文件尚未完成前，不建立公開 JNI API：

- docs/HTTP_FRAMING_POLICY.md
- docs/CANCELLATION_MODEL.md
- docs/JNI_ABI.md
- docs/CONCURRENCY_MODEL.md

這是為了避免 ABI 被未完成的 lifecycle assumptions 固化。
