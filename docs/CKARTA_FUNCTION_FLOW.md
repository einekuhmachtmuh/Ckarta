# Ckarta 函式流程與實作規約

本文件是 Ckarta 自有程式碼的函式流程與實作規約權威文件；`docs/FUNCTION_TRACE.md` 專門保存固定版本 Nginx／Tomcat 的 upstream function-level trace。

## 1. 實際 executable path（目前）

`main()` 目前是唯一可執行的 C 入口，核心 smoke 路徑為：

main
→ parse_arguments
→ ck_config_init
→ ck_config_load_file
→ directive handler: apply_class_path
→ ck_request_init × N
→ ck_runtime_init
→ bootstrap thread
→ JNI_CreateJavaVM
→ CkartaRuntime.start(queueHandle)
→ ck_runtime_dispatch_async_smoke
→ worker thread AttachCurrentThread
→ ck_request_begin
→ NewDirectByteBuffer
→ CkartaRuntime.dispatchAsync
→ bounded Java executor
→ registered JNI publishCompletion
→ native bounded completion queue
→ Linux eventfd notification
→ C epoll_wait / notification drain
→ ck_runtime_poll_completion
→ request terminal publication
→ ck_runtime_shutdown
→ completion queue close
→ CkartaRuntime.stop
→ DestroyJavaVM
→ ck_runtime_destroy
→ ck_config_destroy

另有獨立 native connection lifecycle slice：

ck_connection_init
→ ck_connection_start_async
→ ck_connection_try_terminal
→ ck_connection_close

這是 executable smoke/integration path，不是完整 network／Servlet production path。

## 2. 函式責任規約

| 函式 | caller | primary responsibility | success | failure | ownership |
|---|---|---|---|---|---|
| main | process | orchestration | EXIT_SUCCESS | EXIT_FAILURE | process-local resources |
| parse_arguments | main | CLI syntax | 0/1 | -1 | argv borrowed |
| ck_config_init | main | initialize config | 0 | -1 | allocates class_path |
| ck_config_load_file | main | lexical parse + directive dispatch | 0 | -1 | config owns copied values |
| apply_class_path | config parser | directive semantic validation | 0 | -1 | config-owned path |
| ck_request_init | main/dispatcher | validate/copy descriptor + initialize state | 0 | -1 | descriptor body remains caller-owned/borrowed |
| ck_request_begin | worker | PENDING→RUNNING | 0 | 1/-1 | request owner retained |
| ck_request_cancel | owner | acquire CANCELLING state | 0=本次取得取消 ownership；1=已由其他事件取得/完成或已取消 | -1 | owner remains responsible |
| ck_error_init | error record owner | initialize fixed record | void | no-op on null | caller owns record |
| ck_error_set | error record owner | validate and populate category/code/outcome | 0 | -1 | caller owns record |
| ck_request_fail | failure publisher | RUNNING→FAILING→FAILED | 0 | 1/-1 | winner publishes error |
| ck_request_finish | completion owner | RUNNING→COMPLETED | 0 | 1/-1 | success-only terminal winner |
| ck_request_state | observer | acquire state | valid enum | INVALID sentinel on null | no ownership change |
| ck_completion_queue_init | runtime owner | initialize bounded queue + notification backend | 0 | nonzero | runtime owns queue |
| ck_completion_queue_push_wait | Java JNI publisher | bounded enqueue with close-aware backpressure | 0 | 1/2/error | queue remains owner of copied record |
| ck_completion_queue_pop | C owner | dequeue completion record | 1 | 0/negative | caller receives copied record |
| ck_runtime_init | process control | initialize sync + completion queue + JVM bootstrap | 0 | nonzero | runtime owns initialized state |
| ck_runtime_dispatch_async_smoke | C control | create temporary attached submission worker | 0 | nonzero | worker borrows request lifetime until join |
| ck_runtime_poll_completion | C owner | dequeue native completion and arbitrate request terminal outcome | 1=new terminal success, 0=none, 2=late/duplicate ignored | negative=runtime/identity error, -2=published failure | copied completion record only |
| ck_runtime_completion_fd | C event loop | expose notification fd for event registration | nonnegative fd | -1 | runtime retains ownership |
| ck_runtime_drain_completion_notification | C event loop | drain coalesced OS wake-up state | 0 | negative | does not own completion records |
| ck_runtime_shutdown | process control | close completion queue, request Java stop + join bootstrap | shutdown status | error | sole runtime lifecycle owner |
| ck_runtime_destroy | process control | destroy initialized sync/queue state after shutdown | void | no-op when precondition absent | destroys runtime-owned resources |
| ck_connection_init | connection owner | initialize connection correlation + packed lifecycle | 0 | -1 | caller owns connection storage |
| ck_connection_start_async | connection owner | OPEN→ASYNC_WAIT | 0 | 1/-1 | connection owner retained |
| ck_connection_try_terminal | terminal candidate | atomically publish CLOSING + terminal reason | 0=winner | 1=already terminal, -1=invalid | winner owns close progression |
| ck_connection_close | connection owner | CLOSING→CLOSED | 0=winner | 1=already/non-closable | connection owner releases native resources |
| ck_connection_validate | bridge | validate request/owner/lifetime identity | 0 | 1/-1 | no ownership change |

## 3. Request state machine

`PENDING → RUNNING → COMPLETED`；failure：`RUNNING → FAILING → FAILED`。

`PENDING | RUNNING → CANCELLING`；取消勝出後不得被 completion/failure 覆寫。

## 4. Connection state machine

`OPEN → ASYNC_WAIT → CLOSING → CLOSED`。

`OPEN → CLOSING` 也允許同步 terminal outcome。`CLOSING` 時的 terminal reason 與 state 必須在同一 atomic lifecycle word 一次發布；不能先寫 state 再另寫 reason。

terminal reason：`COMPLETE`、`CLIENT_DISCONNECT`、`TIMEOUT`、`ERROR`、`SHUTDOWN`。

native tokens `request_id`、`owner_token`、`lifetime_token` 只作 correlation/validation identity，不取代實際 owner，也不得暴露成 Servlet application ABI。

## 5. JNI call discipline

每個 JNI sequence 必須：

1. AttachCurrentThread 前確認 thread 尚未依目前 lifecycle 進入 shutdown。
2. FindClass/GetMethodID/Call*/NewDirectByteBuffer 後，在下一個需要正常 JNI state 的操作前檢查 pending exception。
3. 建立 local reference 後，在離開 JNI sequence 前 DeleteLocalRef。
4. DetachCurrentThread 後不得再使用該 JNIEnv*。
5. `JavaVM*` 可以由 runtime-level owner 保存；`JNIEnv*` 不可跨 thread 傳遞。
6. `NewDirectByteBuffer()` 的 address/capacity 必須先滿足 request descriptor 的 native preconditions。
7. `ExceptionClear()` 只在 native layer 已決定接管該 exception 時使用。

## 6. DirectByteBuffer contract

目前 request descriptor 在進入 JNI 前必須滿足：

- body != NULL
- body_length <= INT32_MAX
- body memory 的 lifetime 覆蓋 Java borrow
- Java 只能 read/borrow，不負責 native free

completion record 為固定 value-only layout：request id、owner token、lifetime token、result、status；資料與 notification state 分離。

## 7. Runtime lifecycle

`ck_runtime_init()` 成功表示 completion queue、bootstrap JVM 與 Java runtime start 已成功；此時 runtime mutex/condition、completion queue、bootstrap thread、JavaVM 均處於 owned state。

`ck_runtime_shutdown()` 先關閉 native completion queue 使 blocked producers 可醒來，再通知 bootstrap thread、join，bootstrap thread 再停止 Java runtime 與 DestroyJavaVM。重複 shutdown 在第一次完成後回傳已保存 shutdown status。

`ck_runtime_destroy()` 只允許在 shutdown completed 後銷毀 runtime synchronization primitives 與 completion queue。

## 8. Error mapping

目前 Java completion status：

- `0`：normal completion。
- `-1`：Java-side RuntimeException / application failure → APPLICATION / 500。
- `-2`：executor rejection → RESOURCE / 503。
- 其他非零：目前視為 INTERNAL / 500。

這是 smoke ABI 的 transitional mapping，不是最終 Servlet error mapping。

## 9. Java side flow

`CkartaRuntime.start(queueHandle)` 建立 bounded `ThreadPoolExecutor`，並保存 runtime-owned native completion queue handle；`dispatchAsync()` 僅負責 admission 與提交工作，不在 C event loop 執行 Servlet application code；executor task 建立 `NativeRequest`、驗證 DirectByteBuffer，完成後以 registered JNI native method `publishCompletion()` 直接發布 value-only completion。

`CkartaRuntime.stop()` 停止 executor 後才清除 native queue handle state；native queue 的 shutdown ownership 仍屬 C runtime。

## 10. Completion queue / notification contract

`c/completion/ck_completion_queue.[ch]` 是 bounded multi-producer / single-consumer-oriented process-local queue。producer 以 mutex 保護 ring state，record 與 notification signal 在同一 critical section 完成；`push_wait()` 在 queue full 時等待 `not_full`，在 queue close 時醒來並回傳 closed result。

`c/event/ck_completion_notification.[ch]` 隔離 OS-specific notification backend；Linux 使用 `eventfd(EFD_CLOEXEC | EFD_NONBLOCK)`。notification 可以 coalesce，因此 consumer 必須 drain notification 後反覆 dequeue 至空。

## 11. Error-path normalization rules

1. 一個函式必須明確只有一個 primary owner/cleanup authority。
2. return code 的含義必須在 header/doc/test 中固定，不能由 caller 猜測。
3. state observer 不得用合法 state 值表示 invalid input；invalid state 必須有獨立 sentinel/error return。
4. 每個跨 thread publication 都必須說明 producer → publication → consumer 的 memory ordering。
5. 每個 blocking primitive 都必須記錄 caller thread、阻塞理由、最大等待時間與 shutdown 交互。
6. local JNI references 必須在其 scope 結束前釋放；native buffers 的 owner 不得因 JNI call return 就假定 borrow 結束。
7. error response、diagnostic logging、metrics 與 lifecycle state 是不同輸出，不得互相替代。
8. smoke-only function 名稱／API 不得被 production architecture 文件寫成正式 runtime API。

## 12. Current implementation boundary

目前已完成：request terminal publication、native completion queue、Linux notification backend、Java executor → JNI native completion publisher、C epoll wake、以及 native connection ownership state machine。

目前尚未完成：

- C network/event backend
- HTTP parser
- formal C connection socket/TLS state machine
- real Servlet container hierarchy / mapping
- Servlet request/response facade
- AsyncContext bridge
- response descriptor/output pipeline
- Windows IOCP notification backend
- formal public module ABI
- Servlet 6.1 TCK
- sanitizer/fuzz integration

## 13. Upstream cross-reference

Nginx 1.30.4：event loop、request phase、request finalization、memory pool 的詳細逐函式追蹤見 `docs/FUNCTION_TRACE.md`。

Tomcat 11.0.25：Poller → SocketProcessor → Http11Processor → CoyoteAdapter → Container Pipeline → Servlet，以及 `AsyncContextImpl.complete/timeout/onError/recycle` 的詳細追蹤見 `docs/FUNCTION_TRACE.md`。

Apache HTTP Server 2.4.68：startup/config/MPM 研究見 `docs/STARTUP_CONFIGURATION_RESEARCH.md` 與 `docs/WIN32_LINUX_PLATFORM_RESEARCH.md`。

真人 Tomcat/Servlet 使用者心智模型與 Ckarta 相容性比較見 `docs/TOMCAT_SERVLET_USER_COMPATIBILITY.md`；該文件不是實作規格。

學術依據主要包括 SEDA、Capriccio、ownership types、recovery-oriented computing、exception-handling literature 與 Herlihy/Wing linearizability；各來源完整書目由對應專題文件保存。


## 13. Java async semantic core

目前新增 java/org/ckarta/servlet/CkartaAsyncContext.java 作為正式 Jakarta API adapter 前的 semantic core。它不宣稱實作 jakarta.servlet.AsyncContext，只固定目前可獨立驗證的 application-visible lifecycle concepts：start(Runnable)、complete、timeout、error、client disconnect、shutdown、listener exactly-once notification 與 recycle invalidation。

ACTIVE → COMPLETING|TIMING_OUT|ERRORED 的 CAS 是 async terminal ownership 的單一 linearization point；application 的 complete() 在輸家情況回傳 IllegalStateException，container/internal timeout、error、disconnect、shutdown 的競爭者則只保留唯一 winner，不把正常 race 變成未處理例外。

此 core 仍缺正式 ServletRequest.startAsync()、ServletResponse、AsyncContext.dispatch()、AsyncListener API、ServletContext/classloader 綁定與 native connection bridge。這些必須在正式 Jakarta Servlet 6.1 API dependency 進入 build/test 後逐項接入。


目前 java/org/ckarta/servlet/CkartaServletAsyncContext.java 已建立 Jakarta Servlet 6.1 API binding prototype。它只把 complete、start、request/response access、timeout 與 listener 註冊映射到 Ckarta async core；dispatch 目前明確回傳 UnsupportedOperationException，因正式 request mapping/container dispatch 尚不存在。API adapter 不持有或暴露 native connection pointer、queue 或 lifetime token。故此階段是 API boundary validation，不是 Servlet 6.1 compatibility implementation。
