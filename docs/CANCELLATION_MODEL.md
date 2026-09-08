# Ckarta 取消模型

## 1. 目的

Ckarta 必須處理：

client disconnect（客戶端斷線）
timeout（逾時）
Servlet AsyncContext completion（Servlet 非同步完成）
worker shutdown（工作者停止）
upstream failure（上游失敗）

## 2. Operation state

每一個跨層 operation（跨層操作）至少具有：

PENDING
RUNNING
CANCELLING
COMPLETED
FAILED

狀態轉移必須不可逆。

## 3. Cancellation authority

任何來源都可以提出 cancellation request（取消要求），但最後狀態只能由單一 owner 決定。

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

## 5. AsyncContext

Servlet startAsync 後：

Servlet invocation 可以結束，
但 request/connection bridge 不得立即 free。

只有在：

complete
或 timeout
或 error
或 client disconnect
或 shutdown

其中一個合法 terminal event（終止事件）完成協定後才能釋放相關狀態。

Tomcat 11.0.25 的 CoyoteAdapter.asyncDispatch() 是本設計的重要參考。

## 6. JNI exception

Java exception 不直接穿透成 C pointer（指標）。

JNI bridge 應轉換成明確的 completion/error record。

## 7. Idempotence

cancel(operation) 必須是 idempotent（冪等）的。

第二次 cancel：

不能 double free；
不能重複呼叫 Servlet lifecycle callback；
不能重複關閉同一 native resource。

## 8. Shutdown

worker shutdown：

停止新 request dispatch
→ drain（排空）可完成工作
→ cancel deadline-expired work
→ cancel AsyncContext
→ close connections
→ destroy worker

## 9. 研究依據

Tomcat：

CoyoteAdapter.asyncDispatch()
NioEndpoint.SocketProcessor.doRun()

Nginx：

ngx_http_request.c 中的 request finalize／close 路徑。

學術背景：

Zeldovich 等人的事件驅動多處理器研究支持以明確事件與 ownership 降低並行控制複雜度。

來源：

https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/connector/CoyoteAdapter.java
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_request.c
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs
