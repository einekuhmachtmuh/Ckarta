# Ckarta 函式流程與實作規約

本文件是 Ckarta 自有程式碼的函式流程與實作規約權威文件；`docs/FUNCTION_TRACE.md` 仍專門保存固定版本 Nginx／Tomcat 的 upstream function-level trace。

## 1. 實際 executable path（目前）

`main()` 目前是唯一可執行的 C 入口，實際路徑為：

main
→ parse_arguments
→ ck_config_init
→ ck_config_load_file
→ directive handler: apply_class_path
→ ck_request_init × N
→ ck_runtime_init
→ bootstrap thread
→ JNI_CreateJavaVM
→ CkartaRuntime.start
→ ck_runtime_dispatch_async_smoke
→ worker thread AttachCurrentThread
→ ck_request_begin
→ NewDirectByteBuffer
→ CkartaRuntime.dispatchAsync
→ bounded Java executor
→ CompletionRecord queue
→ ck_runtime_poll_completion
→ request terminal publication
→ ck_runtime_shutdown
→ CkartaRuntime.stop
→ DestroyJavaVM
→ ck_runtime_destroy
→ ck_config_destroy

這是 executable smoke path，不是完整 network／Servlet production path。

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
| ck_runtime_init | process control | initialize sync + bootstrap JVM | 0 | nonzero | runtime owns initialized sync state |
| ck_runtime_dispatch_async_smoke | C control | create temporary attached submission worker | 0 | nonzero | worker borrows request lifetime until join |
| ck_runtime_poll_completion | C owner | dequeue Java completion and arbitrate request terminal outcome | 1=new terminal success, 0=none, 2=late/duplicate ignored | negative=runtime/identity error, -2=published failure | completion values copied out before detach |
| ck_runtime_shutdown | process control | request Java stop + join bootstrap | shutdown status | error | sole runtime lifecycle owner |
| ck_runtime_destroy | process control | destroy initialized sync state after shutdown | void | no-op when precondition absent | destroys runtime-owned sync primitives |

## 3. Request state machine

`PENDING → RUNNING → COMPLETED`；failure：`RUNNING → FAILING → FAILED`。

`PENDING | RUNNING → CANCELLING`；取消勝出後不得被 completion/failure 覆寫。

`RUNNING → FAILING → FAILED` 是 error publication 專用的內部狀態。只有成功 CAS 進入 FAILING 的 publisher 可以寫 `ck_error_t`；完成寫入後以 release-store 發布 FAILED。

`ck_request_error()` 必須在 acquire-load 看到 FAILED 後才取得已發布 error record。

## 4. JNI call discipline

每個 JNI sequence 必須：

1. AttachCurrentThread 前確認 thread 尚未依目前 lifecycle 進入 shutdown。
2. FindClass/GetMethodID/Call*/NewDirectByteBuffer 後，在下一個需要正常 JNI state 的操作前檢查 pending exception。
3. 建立 local reference 後，在離開 JNI sequence 前 DeleteLocalRef。
4. DetachCurrentThread 後不得再使用該 JNIEnv*。
5. `JavaVM*` 可以由 runtime-level owner 保存；`JNIEnv*` 不可跨 thread 傳遞。
6. `NewDirectByteBuffer()` 的 address/capacity 必須先滿足 request descriptor 的 native preconditions。
7. `ExceptionClear()` 只在 native layer 已決定接管該 exception 時使用。

## 5. DirectByteBuffer contract

目前 request descriptor 在進入 JNI 前必須滿足：

- body != NULL
- body_length <= INT32_MAX
- body memory 的 lifetime 覆蓋 Java borrow
- Java 只能 read/borrow，不負責 native free

poll completion output 使用固定 36 bytes：8 + 8 + 8 + 8 + 4，並以 native byte order 編碼。

## 6. Runtime lifecycle

`ck_runtime_init()` 成功表示 bootstrap JVM 與 Java runtime start 已成功；此時 runtime mutex/condition、bootstrap thread、JavaVM 均處於 owned state。

`ck_runtime_shutdown()` 是 lifecycle owner 的操作：設定 shutdown request → wake bootstrap thread → join → 取得 shutdown status。重複 shutdown 在第一次完成後回傳已保存 shutdown status；目前 runtime lifecycle API 不允許與 dispatch/poll 同時競合操作；不得在 destroy 後再次使用 runtime。

`ck_runtime_destroy()` 只允許在 shutdown completed 後銷毀 runtime synchronization primitives。

## 7. Error mapping

目前 Java completion status：

- `0`：normal completion。
- `-1`：Java-side RuntimeException / application failure → APPLICATION / 500。
- `-2`：executor rejection → RESOURCE / 503；completion queue overflow 則另由 queue overflow path 報錯。
- 其他非零：目前視為 INTERNAL / 500。

這是 smoke ABI 的 transitional mapping，不是最終 Servlet error mapping。

## 8. Java side flow

`CkartaRuntime.start(queueHandle)` 建立 bounded `ThreadPoolExecutor`，並保存 runtime-owned native completion queue handle；`dispatchAsync()` 只負責 admission 與提交工作，不在 C event loop 執行 Servlet code；executor task 建立 `NativeRequest`、驗證 DirectByteBuffer，完成後以 registered JNI native method 直接發布 value-only completion record。

`CkartaRuntime.stop()` 先從 runtime registry 移除 executor/completion references，再 shutdown executor；現階段尚未實作真正 Servlet container lifecycle。

`ck_runtime_poll_completion()` 只做 native queue dequeue + request identity validation + terminal publication；不再 attach JVM，也不再輪詢 Java completion queue。

## 9. Configuration flow

`ck_config_load_file()` 只負責讀取、tokenize、directive lookup、handler dispatch 與 validation，不得建立 socket、JVM、worker 或其他不可逆 runtime state。

`ck_config_set_class_path()` 先 allocate/copy 成功後才替換舊值，因此 failure 不會破壞既有有效 configuration value。

目前 configuration 仍是第一階段最小 parser；正式 runtime snapshot/reload 尚未實作。

## 10. Error-path normalization rules

1. 一個函式必須明確只有一個 primary owner/cleanup authority。
2. return code 的含義必須在 header/doc/test 中固定，不能由 caller 猜測。
3. state observer 不得用合法 state 值表示 invalid input；invalid state 必須有獨立 sentinel/error return。
4. 每個跨 thread publication 都必須說明 producer → publication → consumer 的 memory ordering。
5. 每個 blocking primitive 都必須記錄 caller thread、阻塞理由、最大等待時間與 shutdown 交互。
6. local JNI references 必須在其 scope 結束前釋放；native buffers 的 owner 不得因 JNI call return 就假定 borrow 結束。
7. error response、diagnostic logging、metrics 與 lifecycle state 是不同輸出，不得互相替代。
8. smoke-only function 名稱／API 不得被 production architecture 文件寫成正式 runtime API。

## 11. Completion queue / notification contract

`c/completion/ck_completion_queue.[ch]` 是 bounded multi-producer / single-consumer-oriented process-local completion queue：producer 以 mutex 保護 ring state，record 與 notification signal 在同一 critical section 內完成；若 notification backend 回報明確 failure，剛加入的 record rollback。queue overflow 回傳 1，closed queue 回傳 2。`c/event/ck_completion_notification.[ch]` 將 OS-specific notification backend 隔離；目前實作為 Linux `eventfd(EFD_CLOEXEC | EFD_NONBLOCK)`，fd 可交由 epoll 等待。

這個 native primitive 尚未取代 `CkartaRuntime` 現有 Java `ArrayBlockingQueue`，也尚未透過 JNI 接成 production producer path；它目前是下一階段整合的正式候選基線。若未來建立 Windows backend，必須映射到同一 queue/notification contract，而不能修改 request/error/cancellation semantics。

## 12. Current gaps

目前尚未實作：

- C network/event backend
- HTTP parser
- C connection object
- response descriptor
- real Servlet container
- AsyncContext bridge
- production completion notification
- production error response renderer
- formal public module ABI
- Servlet 6.1 TCK
- sanitizer/fuzz integration

因此本文件描述的是「目前已實作函式 + 已固定契約」，不是宣稱完整 Web server 已完成。

## 12. Upstream cross-reference

Nginx 1.30.4：event loop、request phase、request finalization、memory pool 的詳細逐函式追蹤見 `docs/FUNCTION_TRACE.md`。

Tomcat 11.0.25：Poller → SocketProcessor → Http11Processor → CoyoteAdapter → Container Pipeline → Servlet 詳細追蹤見 `docs/FUNCTION_TRACE.md`。

Apache HTTP Server 2.4.68：startup/config/MPM 研究見 `docs/STARTUP_CONFIGURATION_RESEARCH.md` 與 `docs/WIN32_LINUX_PLATFORM_RESEARCH.md`。

學術依據主要包括 SEDA、Capriccio、ownership types、recovery-oriented computing 與 exception-handling literature；各來源的完整書目由對應專題文件保存。