# Ckarta 2026-09-10 文件／原始碼一致性稽核

本文件記錄 2026-09-10 對 `main` HEAD 的第二次 cold-start 一致性稽核結果。它不是新的架構權威；`WORKING_RULES.md`、`docs/WORK_STATE.md` 與各領域 canonical document 的 authority 仍依既有規則。

## 1. 稽核基準

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
- `c/core/main.c`
- `c/platform/ck_socket.[ch]`
- `c/event/ck_event_loop.[ch]`
- `c/event/ck_completion_notification.[ch]`
- `c/connection/ck_connection.[ch]`
- `c/connection/ck_connection_registry.[ch]`
- `c/http/ck_http_connection_reader.[ch]`
- `c/output/ck_http_output_writer.[ch]`
- `Makefile`

Current `main` HEAD at audit time: `633c2d63f8f3316d8ce0ea9a0919a7b6fd52d2f8`.

## 2. 已確認與原始碼一致的主要 current-state 描述

`docs/WORK_STATE.md`、`docs/HOT_PATH_REVIEW.md`、`docs/CONNECTION_OWNERSHIP.md` 與實際 source 一致地描述了：

- C main / worker / event backend / connection ownership / HTTP framing / bounded semantic handoff 的分層。
- generation-protected connection registry 與 reader/output pin lifetime guard。
- Linux epoll executable slice。
- connection-owned HTTP reader、32 KiB read/process budget、request-body bounded FIFO 與 transactional body acknowledgement。
- response transaction、bounded output writer、partial nonblocking send、EPOLLOUT continuation 與 recycle/terminal boundary 分離。
- request ABI v2、Java `NativeRequest` smoke handoff、value-only completion record。
- Java ServletInputStream minimum semantic adapter 尚非完整 production request-body integration。
- Windows IOCP 尚未實作，socket handle representation 尚仍是 Linux `int` baseline。

`c/platform/ck_socket.h` 目前也確實仍公開 `int socket_fd`，而 `ck_connection.h`、`ck_connection_registry.h`、`ck_http_connection_reader.h`、`ck_http_output_writer.h` 均仍把 socket handle 表示為 `int`；這與目前 WORK_STATE 的「完整 socket-handle migration 尚未完成」判斷一致。

## 3. P0：必須先修正的文件內部／跨文件矛盾

### 3.1 `docs/CKARTA_FUNCTION_FLOW.md` 的 Current implementation boundary 已過時

該文件前段明確列出目前 executable path，包含 C event loop、HTTP parser、completion notification、connection lifecycle、Java async semantic core；但 Section 12 又寫成「C network/event backend、HTTP parser、formal C connection socket/TLS state machine、AsyncContext bridge、ServletRequest.startAsync binding、response descriptor/output pipeline 尚未完成」。

這與同文件後續 Java async / native registry 段落、`docs/WORK_STATE.md`、`docs/HOT_PATH_REVIEW.md` 及實際 source 均衝突。

處置：將 Section 12 改寫為「目前已完成的 executable slices」與「仍欠 production integration 的 boundary」，不要再使用早期 pre-implementation 清單。

### 3.2 `docs/JNI_ABI.md` 的 completion notification 狀態過時

Section 18 仍寫「尚未接入正式 production completion dispatch path」並否定目前 `epoll_wait()` 等待 completion notification 的 executable path；但 `docs/WORK_STATE.md`、`docs/CKARTA_FUNCTION_FLOW.md`、`c/core/main.c` 已明確存在：completion queue → eventfd notification → `ck_event_loop_add()` → `ck_event_loop_wait()` → notification drain → completion polling 的 executable smoke path。

需要保留的正確限制是「這仍是 smoke/integration slice，不是正式多 worker／跨平台 production completion backend」，而不是否定 integration 本身不存在。

### 3.3 `docs/CONCURRENCY_MODEL.md` 的 completion notification 描述過時

Section 12 仍寫目前是「Java 有界完成佇列 + C 非阻塞 poll」，並將高效率通知描述為未來工作；這與目前 `c/core/main.c` 的 event-loop wait + completion notification integration，以及 `docs/WORK_STATE.md` 的 current path 不一致。

處置：保留「仍非 multi-worker production routing」限制，但將 Linux eventfd/epoll executable smoke 明確標成已存在。

### 3.4 `docs/EVENT_BACKEND.md` 的 next-gate 與 current TCP slice 重疊過時

Section 10 已列出完整 loopback TCP + accepted connection + generation handle + HTTP request body/pipeline 的驗證；但 Section 11 又說下一個 gate 是先把 listener/accepted connection/registration 提升成正式 connection event consumer，再接 HTTP framing/body。

目前正確描述應是：connection event consumer 已有 executable slice；下一個 gate 是經最新 HEAD CI 驗證後，進入完整 socket-handle contract migration，之後再做 completion/event backend portability。

### 3.5 `docs/REQUEST_HANDOFF.md` 的 Body lifetime prerequisite 過時

該段目前聲稱 HTTP reader 尚未接入 FIFO backpressure，因 parser advance 造成 transactional redesign 尚未完成。

但 `docs/WORK_STATE.md`、`docs/HTTP_CONNECTION_READER.md` 與目前 reader contract 已顯示：pending body、all-or-nothing 64 KiB FIFO write、`CK_HTTP_CONNECTION_READ_BODY_BACKPRESSURE`、ack delay 與同一 pending span retry 已建立並進入 native smoke path。

處置：保留「尚非 ServletInputStream、尚未跨 JNI production integration」；移除「尚未完成 transactional boundary」的舊說法。

### 3.6 `docs/HTTP_CONNECTION_READER.md` 的 Current status 有數項把其他已實作 slice 誤列為「尚未完成」

目前列出：`response/output state machine`、`Servlet request-body stream adapter` 等，會讓 cold-start agent 誤認整個 repository 沒有 output writer 或 Java input-stream adapter。

正確方式應是改成「reader 文件本身不負責」或「reader 與完整 Servlet production integration 尚未完成」，而不是把 repository 已存在的 executable/semantic adapter 說成不存在。

### 3.7 `docs/HTTP_FRAMING_POLICY.md` 的 integration status 過時

文件 Section 8 與 Section 10 仍將 `ck_http_input` 描述成尚未接入正式 connection event loop，但目前 `docs/HTTP_CONNECTION_READER.md` 與 loopback TCP test 已把 HTTP body framing、pipelined leftover 與 request-body FIFO 納入 reader integration slice。

剩餘問題應集中描述為 production multi-worker / full request recycle / security corpus / Servlet body mapping，而不是「沒有 connection integration」。

### 3.8 `docs/WIN32_LINUX_PLATFORM_RESEARCH.md` 的 completion notification 與 verification status 過時

Section 10–11 仍說 eventfd 已存在但尚未與 `ck_event_loop` 正式 wakeup integration、completion-to-event-loop wakeup 尚未整合；目前 `c/core/main.c` 已明確建立該 executable smoke path。

Section 16 又直接稱 Linux `ck_event_loop` 與 loopback TCP integration 已由 GitHub Actions 驗證；但目前最新 HEAD 的 run #865 狀態仍是 `queued`，因此不能用該文件維持「current HEAD verified」的語氣。

應改成「目前已實作 executable slice；current HEAD 最新 CI verification pending」。

## 4. P1：文件架構描述需重新標示 current vs target

### 4.1 `docs/ARCHITECTURE.md`

該文件本質上是 target architecture，這本身合理；但應在 section heading 或開頭明確標明「target architecture / normative design direction」，避免 cold-start agent 把尚未實作的 TLS/static/proxy/container hierarchy 解讀為 current capability。

另有 cross-boundary step 編號重複兩次 `5.`，應修正為連續編號。

### 4.2 `docs/FUNCTION_TRACE.md`

該文件定位為固定版本 Nginx/Tomcat upstream function-level trace，但最後 Section 11 仍保留「建立第一個 C network module 前必須完成」的舊前置清單；目前 C network/event/HTTP native slices 已存在，這段應改為 historical prerequisite / superseded gate，或直接移除並以 `docs/WORK_STATE.md` 的 current gate 取代。

### 4.3 `docs/DESIGN_DECISIONS.md`

目前 repository 已有 request ABI v2、Makefile build contract、completion routing與多個實作 slice，但其「尚未決定」段落仍含過寬的舊決策清單。應改成只列真正尚待決定事項，避免與 current source / WORK_STATE 分叉。

## 5. P1：工作樹描述不符合目前 repository

`docs/WORKING_TREE.md` 現仍寫「除 `error/` 外，多數目錄目前仍屬架構規劃」。實際 tree 已有大量 executable C/Java implementations，包含：

- `c/event/`
- `c/completion/`
- `c/connection/`
- `c/http/`
- `c/output/`
- `c/jni/`
- `c/platform/`
- `java/org/ckarta/servlet/`
- 大量 `tests/` 與對應 Makefile targets

因此應將該句改為「部分子模組仍屬規劃；以下列出的 current executable slices 已有 implementation/tests」，並列出主要已實作目錄。

## 6. P1：學術書目一致性問題

`docs/FUNCTION_TRACE.md`、`docs/THREAD_MODEL.md` 對 Capriccio DOI 使用了不同字串：前者 `10.1145/945445.945471`，後者 `10.1145/945469.945471`。目前不能僅依 repository 內部選一個並假定另一個是 typo；應在 canonical research source 直接確認 ACM metadata 後統一。

SEDA 亦同時存在 SOSP proceedings DOI 與 ACM SIGOPS article DOI 的雙版本情況；使用時應明確標示 publication version，而非把兩個 DOI 當成互斥資料。

Timing Wheels 亦需在引用時明確區分 1987 proceedings 與 1997 IEEE/ACM Transactions on Networking 版本，不應只留下未標版本的單一 DOI。

## 7. Socket portability source-level conclusion

本次 source-level 交叉核對再次確認，目前下一個 C/native portability code slice 應是一次完成的 socket handle contract migration，而不是單檔型別替換。

目前：

- `ck_socket_*` platform wrapper 已集中 Linux/POSIX socket calls。
- `ck_connection_t.socket_fd` 仍是 `int`。
- registry attach/socket lookup 仍採 `int`。
- HTTP reader/output writer API 仍採 `int`。
- event loop `epoll_fd` 與 socket registration API 仍採 Linux `int` descriptor。
- completion notification 則另有 Linux `eventfd` `int fd`。

因此不能現在把 `int` 單純 rename 成 `ck_socket_t` 就宣稱 Win32 portability；socket handle、invalid sentinel、socket result/error contract、event registration boundary、close semantics、tests 必須同一個 cohesive slice 處理。

## 8. CI gate

`main` HEAD `633c2d63f8f3316d8ce0ea9a0919a7b6fd52d2f8` 對應 GitHub Actions `build-smoke` run `34426880480`（run #865），目前 status 為 `queued`、conclusion 為 `null`。

因此本次稽核後不得：

- 將最新 HEAD 的 native/Java slices 升級為 current verified gate；
- 開始下一個 production socket-handle migration；
- 用較舊 run 的成功結果替代 current HEAD verification。

在 current HEAD CI 真正通過前，可以安全執行的是 documentation reconciliation、source-level audit、reference verification 與不改變 runtime contract 的研究工作。

## 9. 結論與下一步

本次稽核已足以判定：目前 repository 的「實際原始碼狀態」比部分舊文件描述得更先進，但 cold-start 文件仍未達完全一致。

因此本次不進行 socket-handle migration，也不進行其他 production runtime architecture change。

立即下一步應是：

1. 依本稽核逐一修正上述 stale/contradictory MD，優先修正 P0 文件。
2. 重新取得每個目標 MD 最新 blob SHA，逐檔做受控整合，不覆蓋未知變更。
3. 每個文件修改後檢查 diff，並確認 `WORK_STATE.md` 的 current gate、source implementation description 與 test/build contract 重新一致。
4. current HEAD CI 通過後，再重新執行一次 socket portability rescan，然後才開始 cohesive socket-handle contract migration。
