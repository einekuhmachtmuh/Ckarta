# Ckarta 取消模型

## 1. 目的

Ckarta 必須處理：

client disconnect（客戶端斷線）
timeout（逾時）
Servlet AsyncContext completion（Servlet 非同步完成）
worker shutdown（工作者停止）
upstream failure（上游失敗）

## 2. Operation state

每一個跨層 operation 至少具有：

PENDING
RUNNING
CANCELLING
COMPLETED
FAILED

成功路徑：`PENDING → RUNNING → COMPLETED|FAILED`。
取消路徑：`PENDING|RUNNING → CANCELLING`；一旦 cancellation 勝出，不得覆寫成另一個 terminal state。

## 3. Cancellation authority

任何來源都可以提出 cancellation request，但最後 terminal state 與資源回收責任只能由單一 owner 決定。

目的：

避免：

timeout → free
client close → free

造成 double free（二次釋放）。

## 4. Connection close

client disconnect 時：

C connection
→ mark closing
→ signal Java／Servlet side
→ prevent new dispatch
→ finish or cancel pending async work
→ release native resources

目前 native connection slice 把上述 terminal ownership 具體化為：

`OPEN | ASYNC_WAIT → CLOSING → CLOSED`

第一個合法 terminal event 透過單一 atomic lifecycle word 取得 ownership；terminal reason 與 closing state 同時發布，後續競爭事件不得覆寫。

## 5. AsyncContext

Servlet `startAsync()` 後：

Servlet invocation 可以結束，
但 request/connection bridge 不得立即 free。

只有在：

complete
或 timeout
或 error
或 client disconnect
或 shutdown

其中一個合法 terminal event 完成協定後才能釋放相關狀態。

目前 Ckarta 已有 native connection ownership state machine，但尚未把真正的 Jakarta Servlet 6.1 `AsyncContext` 事件接入；這是下一個 integration gate。

Tomcat 11.0.25 的 `CoyoteAdapter.asyncDispatch()` 與 `AsyncContextImpl` 是本設計的重要 reference。Tomcat `AsyncContextImpl` 對 `complete()`、`timeout()`、`onError`、`onComplete`、recycle 與 concurrent-use protection 分別處理，不能簡化成一個背景 thread callback。

## 6. JNI exception

Java exception 不直接穿透成 C pointer。

JNI bridge 應轉換成明確的 completion/error record。

## 7. Idempotence

cancel(operation) 必須是 idempotent。`ck_request_cancel()` 對已取消／已終止 operation 重複呼叫無副作用；`ck_request_finish()` 不會覆寫已勝出的 cancellation。

connection terminal ownership 同樣只能取得一次；`ck_connection_close()` 第二次呼叫回傳 non-owner result，不重複關閉 native resource。

不能 double free；不能重複呼叫 Servlet lifecycle callback；不能重複關閉同一 native resource。

## 8. Shutdown

worker shutdown：

停止新 request dispatch
→ drain 可完成工作
→ cancel deadline-expired work
→ cancel AsyncContext
→ close connections
→ destroy worker

正式實作仍需把 native completion queue close、Java executor stop、AsyncContext cancellation 與 connection close 的順序固定下來；不能依 thread timing 猜測。

## 9. 研究依據

Tomcat：

CoyoteAdapter.asyncDispatch()
AsyncContextImpl
NioEndpoint.SocketProcessor.doRun()

Nginx：

ngx_http_request.c 中的 request finalize／close 路徑。

學術背景：

Zeldovich 等人的事件驅動多處理器研究支持以明確事件與 ownership 降低並行控制複雜度；Clarke、Potter、Noble 的 ownership types 研究提供 alias／ownership containment 的形式化背景。本專案不把這些研究視為 JNI correctness proof。

Herlihy／Wing 的 linearizability 可作 concurrent terminal arbitration 的 correctness baseline；目前 connection CAS 只是可檢驗的線性化點候選，並非形式化 proof。

來源：

https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/connector/CoyoteAdapter.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/AsyncContextImpl.java
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_request.c
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs
https://doi.org/10.1145/78969.78972

## 10. Current implementation boundary

目前 C request lifecycle 已具備 atomic state、idempotent cancellation 與 terminal ownership gate；`c/connection/ck_connection.[ch]` 已提供 connection-level native ownership gate 與 token validation；Java Servlet AsyncContext 的跨執行緒 cancellation、connection close、response ownership 與 recycle-compatible invalidation 仍待 integration test。


## 11. Cross-layer terminal arbitration implementation

本輪將取消／完成／timeout／error／client disconnect／shutdown 的第一個跨層 arbitration primitive 落實為 native registry + Java terminal gate。native connection registry 對每一 async cycle 先做 identity validation，再以 `ck_connection_try_terminal()` 決定 CLAIMED、ALREADY_SAME 或 ALREADY_DIFFERENT。

Java `CkartaAsyncContext` 在有 native capability 時，必須先通過 `TerminalGate`；native LOST 不得進入 Java local terminal transition。只有 CLAIMED 或 ALREADY_SAME 才可把 Java state 推進至 local terminal state。這使 native owner 成為跨層 terminal authority，而 Java state 是對合法 native outcome 的語意反映。

此設計與 Servlet 6.1 AsyncContext 的 per-cycle 模型一致，但目前只驗證 cycle identity 與 terminal ownership，不包含完整 timeout scheduling、error dispatch、async dispatch、新 cycle reinitialization、response close 或 real client disconnect。
