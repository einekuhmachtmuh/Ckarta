# Ckarta 2026-09-10 文件／原始碼一致性稽核後續

本文件記錄在 `docs/CONSISTENCY_AUDIT_2026-09-10.md` 之後，對目前 `main` 再次進行的 cold-start 一致性稽核。它不是架構權威；規則與 authority 仍依 `WORKING_RULES.md` 及各領域 canonical document。

## 1. 稽核範圍

本次重新核對：

- `WORKING_RULES.md`
- `docs/WORK_STATE.md`
- `docs/ARCHITECTURE.md`
- `docs/HOT_PATH_REVIEW.md`
- `docs/CONNECTION_OWNERSHIP.md`
- `docs/THREAD_MODEL.md`
- `docs/CONCURRENCY_MODEL.md`
- `docs/JNI_ABI.md`
- `docs/CKARTA_FUNCTION_FLOW.md`
- `docs/FUNCTION_TRACE.md`
- `docs/EVENT_BACKEND.md`
- `docs/REQUEST_HANDOFF.md`
- `docs/HTTP_FRAMING_POLICY.md`
- `docs/HTTP_CONNECTION_READER.md`
- `docs/HTTP_OUTPUT_WRITER.md`
- `docs/CANCELLATION_MODEL.md`
- `docs/WIN32_LINUX_PLATFORM_RESEARCH.md`
- `docs/WIN32_SOURCE_AUDIT_2026-09-10.md`
- `docs/WORKING_TREE.md`
- `c/core/main.c`
- `c/platform/ck_socket.[ch]`
- `c/event/ck_event_loop.[ch]`
- `c/event/ck_completion_notification.[ch]`
- `c/completion/ck_completion_queue.[ch]`
- `c/connection/ck_connection.[ch]`
- `c/connection/ck_connection_registry.[ch]`
- `c/http/ck_http_input.[ch]`
- `c/http/ck_http_connection_reader.[ch]`
- `c/output/ck_http_output_writer.[ch]`
- `tests/connection/ck_connection_test.c`
- current GitHub Actions `build-smoke` state

並重新交叉確認固定 Nginx 1.30.4、Tomcat 11.0.25、Linux epoll/eventfd/close 語意，以及 libuv Windows handle model 與相關學術來源。

## 2. 文件↔文件結論

目前最重要的 current-state authority `docs/WORK_STATE.md` 已正確描述：

- Linux epoll + loopback TCP executable slice。
- connection-owned reader/output 與 lifetime pin。
- request ABI v2。
- Java executor → JNI completion publisher → bounded native completion queue → Linux eventfd → C event backend wait 的 executable smoke path。
- transactional request-body pending/ack 與 64 KiB bounded FIFO。
- ServletInputStream minimum semantic adapter，但尚未完成 production body integration。
- Windows IOCP 尚未實作。
- socket handle representation 仍為 Linux `int` baseline。

`docs/WORKING_TREE.md` 已在前一 commit 修正，不再把實際存在的 event/connection/http/output/platform/jni/java implementation slices 誤寫成純規劃骨架。

但以下 canonical documents 仍有明確 current-state drift，尚未在本次全部重寫：

1. `docs/CKARTA_FUNCTION_FLOW.md` Section 12 仍把已存在的 HTTP/event/Async/output slices 部分列為尚未實作。
2. `docs/JNI_ABI.md` Section 17–18 仍把 eventfd → epoll completion notification 寫成下一階段，否定目前 executable integration。
3. `docs/CONCURRENCY_MODEL.md` completion notification 描述仍偏向 polling-only smoke，未反映目前 eventfd/epoll wake path。
4. `docs/EVENT_BACKEND.md` next-gate 仍重複已存在的 listener/connection/HTTP integration；正確下一 gate 應是 current-head verification 後的 socket-handle migration。
5. `docs/REQUEST_HANDOFF.md` body lifetime prerequisite 仍保留舊的 transactional redesign wording。
6. `docs/HTTP_FRAMING_POLICY.md` integration status 仍低估現有 connection-reader/body integration。
7. `docs/WIN32_LINUX_PLATFORM_RESEARCH.md` Section 10–11 仍把 completion notification integration 寫成未完成；Section 16 的 verification wording 也不能直接代表目前 HEAD。
8. `docs/HTTP_CONNECTION_READER.md` 的「尚未完成」列表仍需要區分「reader 模組不負責」與「repository 尚不存在」。
9. `docs/ARCHITECTURE.md` 屬 target architecture，但應更明確標出 target/current boundary，且仍有跨 boundary step 編號重複 `5.`。
10. `docs/FUNCTION_TRACE.md` 與 `docs/DESIGN_DECISIONS.md` 仍有歷史 gate／過寬未決事項需要改成 historical/superseded 或真正 current pending items。

這些都是文件一致性問題，尚未構成新的 runtime architecture blocker。

## 3. 文件↔原始碼結論

目前 executable path 與 source 一致：

`c/core/main.c` 已實際建立：

`ck_runtime_dispatch_async_smoke()`
→ `ck_event_loop_init()`
→ `ck_event_loop_add(ck_runtime_completion_fd(...), ...)`
→ `ck_event_loop_wait()`
→ `ck_runtime_drain_completion_notification()`
→ `ck_runtime_poll_completion()`。

因此不能再把 completion notification 稱為僅有 primitive 而未與 event backend 接通；正確限制是「已有單一 executable smoke integration，但不是 production multi-worker/cross-platform final backend」。

`c/completion/ck_completion_queue.c` 也確實在 enqueue completion record 的同一 critical section 內 signal eventfd；consumer 必須 drain notification 後繼續 dequeue 至空。這與 `WORK_STATE` 的 coalesced-notification 描述一致。

`c/http/ck_http_input.c` + `c/http/ck_http_connection_reader.c` 已實作 pending-body transactional acknowledgement：body consumer 暫時 backpressure 時不前移 reader buffer；ack 後才提交 pending input consumption。因此舊文件中「parser advance 後尚未能安全 retry」的 current-state 描述已失效。

`c/connection/ck_connection.c`、`ck_http_connection_reader.c`、`ck_http_output_writer.c` 仍共同採 `int socket_fd`，與 `WORK_STATE` 的「跨平台 socket-handle migration 尚未完成」一致。

`ck_connection_t`、reader、output writer 與 registry pin 的 owner/lifetime boundary 目前互相一致：registry mutex 只保護 entry lookup/lifetime counter，不應包住 socket I/O；真正 I/O 由 connection owner 執行。

## 4. 原始碼↔原始碼結論

### 4.1 Completion queue ↔ event loop

目前 coupling 是正確的：completion queue 只暴露 notification fd 與 drain 操作；event loop 負責等待；runtime 負責 routing completion records。event loop 沒有取得 Java object ownership，也沒有把 Servlet application code 拉入 event thread。

### 4.2 HTTP input ↔ reader

`ck_http_input_feed()` 在 pending body 時返回相同 pending span，`ck_http_input_ack_body()` 才移除 pending state；reader 在 sink 成功後才遞增 `begin`。這與 bounded FIFO 的 all-or-nothing write contract 相容，不會在正常 backpressure retry 下重播 body prefix。

### 4.3 Connection ↔ registry

registry 在 close/retire 前檢查 reader/output user count；reader/output pin 在 registry lock 內增加 user count、離鎖後才進行 I/O。這與 connection single-owner/lifetime guard 模型一致。

### 4.4 Connection close semantics

`ck_connection_close()` 成功把 lifecycle 轉成 `CLOSED` 後立即把 `socket_fd` 設為 `-1`，再呼叫 `ck_socket_close()`。Linux `close(2)` 文件指出 Linux 會在 close 流程早期釋放 fd，即使之後回報 `EIO` 等錯誤也不得再次 retry close；因此目前 source 並不能直接判定為 double-close 或必然 fd leak。

但 `ck_connection_close()` 對 `EINTR` 回傳 `2`、對其他 close error 回傳 `-1`，同時 lifecycle 與 native resources 已經進入 closed/released state。這屬於 return-code/diagnostic semantics 尚需在 `CONNECTION_OWNERSHIP`、`ERROR_STATE_MATRIX` 與 socket abstraction 中統一的問題；不是現在急著改 close algorithm 的理由。

### 4.5 Registry raw socket accessor

`ck_connection_registry_socket_fd()` 會在釋放 registry lock 後把 raw fd 交回 caller。這個 API 本身不能被解讀為已取得 I/O lifetime ownership；真正 reader/output path 必須使用 corresponding pin contract。下一個 socket-handle migration 前應決定是否保留此 accessor、是否改成只供 diagnostics、或改成與 pin/lifetime 明確綁定。

## 5. Upstream / kernel cross-check

固定 Nginx 1.30.4 的 epoll backend 以 Linux epoll interest/ready-list model 實作 event dispatch；Windows source 則使用 `SOCKET` 與 Winsock extension，例如 AcceptEx/TransmitFile/ConnectEx。這再次支持「platform-native backend + Ckarta-owned semantic contract」，不支持把 Linux `int` descriptor 視為跨平台共同 handle。

固定 Tomcat 11.0.25 的 Nio2 endpoint 使用 `AsynchronousServerSocketChannel`、`AsynchronousSocketChannel` 與 `CompletionHandler`，再次支持 completion-oriented Windows/async backend 不應被硬套成 Linux readiness fd model。

Linux epoll 文件確認 `epoll_wait()` 從 ready list 取得 readiness event；`epoll_ctl()` 的 `data` 可攜帶 opaque user data。因此目前 Ckarta 將 generation/correlation cookie 放入 event data，而不是存放裸 connection pointer，方向一致。

Linux eventfd 文件確認 eventfd 可被 epoll/poll 等 multiplexing API 監控，因此 completion queue 使用 eventfd 後由 `ck_event_loop` 等待在 OS 語意上成立。

Linux close 文件確認 Linux 會早期釋放 fd，close error 不應被實作成第二次盲目 close；這正是目前 `ck_connection_close()` 不能被簡單判定為「錯誤就一定洩漏」的原因。

libuv Windows source 明確分別使用 `SOCKET`、`HANDLE`、`uv_os_sock_t` 與 `uv_os_fd_t`，其 IOCP loop 欄位也是 `HANDLE`。這與下一步把 socket handle 與 event/completion object 分成不同 platform contracts 的方向一致。

## 6. 學術 evidence cross-check

SEDA 的核心研究與目前 bounded queue/backpressure/fairness 語意相容，但 SEDA 不構成 Ckarta 必須採 central bridge queue 的證明。其學術出版 metadata 應依 publication version 明確標示。

Zeldovich 等人的 `Multiprocessor Support for Event-Driven Programs` 支持以 coarse-grained parallelism 與 serialized ownership 取得多處理器擴展，但不要求 Ckarta 採某一固定 worker/bridge topology。

Clarke、Potter、Noble 的 ownership types 工作支持以 ownership/alias containment 思考 native/Java lifetime boundary，但不直接證明任何特定 C ABI 正確；Ckarta correctness 仍須由自身 state machine、memory ordering 與測試證明。

Capriccio 可作為 thread-based server 的反例證據，說明不能因選 event-driven data plane 就先驗排除 attached-worker 或 bridge topology；最終 topology 仍應由 Ckarta benchmark 決定。

## 7. Current CI gate

目前 `main` 最新 HEAD 為本次文件 reconciliation commit；對應 `build-smoke` run #867，run id `34429156874`。截至本稽核時該 run 仍為 `queued`、`conclusion=null`。

因此：

- 不得把最新 HEAD 升級成 verified gate。
- 不得以舊 run 成功替代最新 HEAD verification。
- 不得現在開始 socket-handle migration。
- 可以繼續做 documentation reconciliation、source-level audit、reference verification 與不改變 runtime contract 的研究。

## 8. 下一步判定

本次稽核沒有發現需要立即中止整體架構的 P0 runtime defect，但發現兩類必須在 socket migration 前收斂的事項：

1. 剩餘 canonical MD 的 current-state reconciliation。
2. socket API 中 raw handle、invalid sentinel、close/error return semantics、event registration lifetime 與 pin ownership 的完整 contract consolidation。

因此本次不進入 socket-handle migration。安全下一步是完成剩餘 stale canonical MD reconciliation；待最新 HEAD CI 通過後，再進行一次只針對 socket contract 的 source-level rescan，若無新 blocker 才開始 cohesive socket-handle migration。
