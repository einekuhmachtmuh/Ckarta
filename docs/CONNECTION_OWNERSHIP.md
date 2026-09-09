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
- connection timer references
- input/output buffer references
- current request reference
- lifecycle state

`ck_event_loop` 則擁有其 epoll instance；connection 不直接擁有 epoll fd。connection owner 負責決定何時把其 socket registration 加入、修改或移除 event loop，但 event backend 不因此取得 socket ownership。

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

Request owner 是 connection，直到 request 被明確移交至 asynchronous owner。`owner_token`、`lifetime_token` 與每一 async cycle 的 `cycle_id` 是驗證用識別，不取代實際 owner；native storage 的回收責任仍屬 owner。`cycle_id` 必須在其 native 可表示範圍內，且同一 request 的不同 async cycle 不得共用相同 active cycle identity。

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

目前 correlation path 為：

```text
Servlet service()
        ↓ startAsync()
connection OPEN → ASYNC_WAIT
        ↓
AsyncContext.complete / error / timeout / disconnect
        ↓
native terminal candidate
        ↓
CLOSING → CLOSED
```

真正的 Servlet API 行為仍以 Servlet 6.1 規格為準；上述圖只是 Ckarta native ownership contract，不代表完整 AsyncContext implementation。

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

目前 `ck_event_loop_t` 另外有 single-owner contract：同一 instance 在任一時間只能由一個 C event-loop owner thread 呼叫 event registration／modification／removal／wait／destroy；不得將目前 API 當作 thread-safe shared object。

## 13. 暫時禁止

在下列文件尚未完成並審查前，不建立正式 public JNI API：

- docs/HTTP_FRAMING_POLICY.md
- docs/CANCELLATION_MODEL.md
- docs/JNI_ABI.md
- docs/CONCURRENCY_MODEL.md

目前 `publishCompletion` 屬於 runtime-internal registration，用於 executable smoke/integration slice，不應視為穩定 public module ABI。

## 14. Executable lifecycle slice

`c/jni/ck_request.[ch]` 將 descriptor 與 atomic lifecycle state 分離。同步 smoke path 的 descriptor/body 由 caller 擁有，worker 只借用；caller 在 `pthread_join()` 後才離開其生命週期。

`c/connection/ck_connection.[ch]` 現在同時提供 native connection ownership state machine、token/cycle validation 與 Linux/POSIX socket descriptor ownership；它尚未管理 TLS 或 Servlet request object。

## 15. AsyncContext bridge integration

Tomcat 11.0.25 `AsyncContextImpl` 顯示真正 async lifecycle 還包含 `start()`、`complete()`、`timeout()`、`onError()`、`onComplete()`、recycle 與 concurrent-use protection；其中 recycle 必須先建立不可再用的狀態標記，再清除 request/context 等欄位。Ckarta 因此不得只把 `startAsync()` 當成「把 request 放進背景 thread」。

目前已完成：

1. Java `CkartaServletRequestAdapter` 的 Servlet 6.1 `startAsync()` binding prototype。
2. `CkartaAsyncCycleBinding` 的 per-cycle identity。
3. generation-protected native connection registry／opaque handle。
4. C→Java JNI native terminal bridge。
5. native terminal result 的 CLAIMED／ALREADY_SAME／ALREADY_DIFFERENT 三分法。
6. native connection object 的實際 Linux/POSIX socket descriptor ownership。
7. C-driven JVM integration test 與 `socketpair()` EOF 驗證。
8. Linux/POSIX `ck_event_loop` 的獨立 epoll backend smoke slice。

仍待完成：

1. timeout/error/client-disconnect 的真正 event source 與 Servlet precedence。
2. response/output ownership。
3. cross-thread cancellation。
4. post-recycle invalidation 的完整 API semantics。
5. async dispatch / new-cycle reinitialization。
6. shutdown drain。
7. Servlet 6.1 TCK compatibility tests。
8. listener／accepted connection 與 event registration 的正式整合。
9. notification consumer 的 generation/cookie validation 與 stale-event rejection。

固定 Tomcat source：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/AsyncContextImpl.java

學術 correctness 基線：Herlihy/Wing 的 linearizability。來源：https://doi.org/10.1145/78969.78972

## 16. Async semantic core boundary

Java side currently has a CkartaAsyncContext semantic core，但尚未完成 public Jakarta Servlet AsyncContext 的全部 semantics。其 terminal events 透過受控 gate 進入 native connection，application code 不取得 native pointer。

Java semantic core 不擁有 native memory；native connection owner 負責 descriptor、buffer 與 connection resource release。Java 只提出 semantic terminal request。

## 17. Native connection registry and opaque handle

C connection object 現由 process-local registry 管理 lifecycle lookup。registry 是 connection capability 的唯一 lookup authority；Java 只保存 opaque generation handle，不取得 `ck_connection_t *`，也不取得 socket/TLS/native pool ownership。

handle layout 為低 32 bits slot identifier、高 32 bits generation。retire 後 entry inactive；重新使用 slot 時 generation 遞增，因此舊 handle 不得命中新 connection。registry 在所有 lookup、identity validation、start-cycle、terminal、close、retire 操作上使用同一 mutex 保護 entry lifetime；connection 的 lifecycle state 仍由 atomic packed word 發布。

跨層 terminal ordering 為：先 native registry terminal arbitration，再由 Java semantic core 更新 local async state。若 native 已先因相同 event 取得 terminal ownership，Java 後續收到同一 event 時可回傳 ALREADY_SAME 並正常完成 local publication；若不同 event 已先勝出，Java 不得建立第二個 terminal outcome；若 registry/identity 發生錯誤，必須走獨立 bridge error path。

JNI integration test 已驗證 stale handle、capacity、cycle identity、complete winner、client-disconnect winner 與 delayed same-event notification。仍未接入真正 TLS owner，也未證明完整 Servlet response lifetime。

## 18. Native socket descriptor ownership slice

`ck_connection_t` 現已持有 Linux/POSIX `socket_fd`。`ck_connection_attach_socket()` 僅允許對仍為 `OPEN` 且尚未持有 descriptor 的 connection attach；registry 的對應 wrapper 會先執行 handle 與 request/owner/lifetime identity validation。

terminal owner 成功把 lifecycle 從 `CLOSING` 推至 `CLOSED` 後，只有該 caller 執行 descriptor close，並立即把 `socket_fd` 設為無效值。其他 caller 看到 `CLOSED` 只能得到 already-closed 結果，不會再次 close 同一 descriptor。registry retire 另外要求 connection 已 `CLOSED` 且 descriptor 已失效後才可釋放 entry。

目前 event registration 不改變 socket ownership：connection owner 仍擁有 fd；event loop owner 擁有 epoll instance；registration 的建立／修改／移除只能由 event-loop owner 執行。關閉 connection 前，正式 network path 必須先依 registration contract 移除或失效其 event registration，再進行 descriptor close 與 registry retire，避免 stale notification 與 fd reuse 相互混淆。

目前 executable scope 是 Linux/POSIX baseline；Windows `SOCKET`／IOCP 尚未提前塞入 connection core。這不是 public ABI 決策，而是後續 platform event backend 的獨立 gate。

`tests/connection/ck_connection_test.c` 與 `tests/connection/ck_connection_registry_test.c` 使用 `socketpair(AF_UNIX, SOCK_STREAM, ...)` 驗證 attach ownership、wrong-token rejection、terminal close、descriptor invalidation、peer EOF 與 close/retire ordering。

Linux event backend 的獨立驗證見 `docs/EVENT_BACKEND.md` 與 `tests/event/ck_event_loop_test.c`；該 test 尚未證明 connection registry 與 epoll registration 的完整 lifetime ordering。


## 19. Native response writer ownership

`ck_connection_t` 現同時持有 heap-backed `ck_http_connection_reader_t` 與 `ck_http_output_writer_t`。兩者都屬 connection resource，均在 connection 初始化時建立，並於 terminal close 後由 connection owner 回收。

output writer 借用同一 `socket_fd`；writer 本身不得 close socket。socket descriptor 的唯一 close authority 仍是 connection owner。

registry 現提供獨立 `ck_connection_registry_output_pin_t`。output pin 的取得只在 registry mutex 內完成 handle/generation/identity validation 與 user counter increment，取得後即釋放 registry mutex；真正 `send()` 不得在 registry mutex 內執行。只要 output pin 存在，registry `close()` 與 `retire()` 均不得回收 connection。

因此 response writable notification 的安全路徑為：

epoll cookie
→ registry identity validation
→ output pin acquire
→ unlock registry
→ bounded non-blocking writer drive
→ output pin release
→ 必要時更新 EPOLLOUT interest

這只保護 native writer/connection lifetime，不等同 writer thread-safe；同一 connection output state 仍以單一 worker owner 推進為原則。
