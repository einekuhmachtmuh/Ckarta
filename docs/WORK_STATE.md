# Ckarta 工作現況

本文件只保存跨對話仍需要的「目前有效狀態、決策、限制、驗證閘門與分支狀態」。它不是 Git changelog，也不是各專題文件的第二權威來源。

歷史提交、commit message、PR、merge、CI chronology 與已 supersede 的中間狀態由 Git/GitHub 保存；需要追溯變更來源時直接查 Git provenance。本文件只在歷史資訊仍構成目前有效 invariant 時保留必要摘要。

目前 `main` HEAD：`93988c2996aede7af145c5b1b50e74cfadd56552`。

## 1. 工程基線

- 正式產品程序入口：C `main()`；Java `main()` 僅供測試／工具。
- JVM：OpenJDK 21；JNI 是主要進程內整合邊界。
- Servlet 目標：Jakarta Servlet 6.1；未通過 TCK 前不得宣稱相容。
- Linux reference backend：epoll baseline 已有 executable listener/connection integration。
- Windows：IOCP 為正式候選 backend；尚未實作。
- 固定 Nginx reference：1.30.4，commit `017cf98dcce217946572a896f0992370475e189f`。
- 固定 Tomcat reference：11.0.25，commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- Apache HTTP Server 2.4.68 僅作 external research baseline，不是目前 submodule。

## 2. 架構責任

```
C main/control
→ C worker + platform event backend
→ connection ownership
→ HTTP parser/framing
→ bounded semantic handoff
→ Java Servlet executor/container
```

C 負責 socket、event backend、HTTP framing、connection lifecycle、native buffering、output I/O 與 platform integration。

Java 負責 Servlet semantics、web application lifecycle、application execution、session、class loading/deployment 與 application-facing request/response API。

C event-loop thread 不得執行 Servlet application code。

## 3. 已建立並有可執行驗證的 native path

### Network/input

```
loopback TCP
→ accept4(SOCK_NONBLOCK|SOCK_CLOEXEC)
→ generation-protected connection registry
→ epoll readiness
→ bounded recv
→ HTTP/1.1 parser
→ Content-Length / chunked framing
→ keep-alive recycle
```

已有 connection reader、registry reader/output pin、bounded read/process budget，以及 pipeline preservation。

### Output

```
response transaction
→ HTTP final-response header serialization
→ bounded output writer
→ partial nonblocking send
→ EPOLLOUT continuation
→ output drain
→ HTTP connection recycle
```

`FINISHED`、`DRAINED`、connection recycle 與 terminal close 是不同 lifecycle boundary。

### Request handoff

目前 canonical request ABI 是 version 2：

```
HTTP parser
→ ck_request_init_http()
→ compact metadata + body views
→ JNI dispatchAsync()
→ Java NativeRequest
```

metadata 為 request-owned bounded storage；body 若非零長度仍受 native owner lifetime 約束。

### Error/cancellation/correlation

native error record、request terminal publication、connection registry generation、owner/lifetime token、async cycle identity 與 exactly-once terminal arbitration 已形成基礎 contract；正式 Servlet AsyncContext ↔ production connection cancellation 尚未完成。

## 4. 目前 request-body gate

目前 HTTP input 已增加 transactional pending-body acknowledgement：

```
feed
→ parser/input state
→ pending body
→ body consumer
→ ack_body()
→ publish consumed/message completion
```

這避免 consumer 暫時拒絕 body 時重新 feed 同一 bytes 而重播 parser state。

目前已有 64 KiB bounded SPSC body FIFO，並已接入 connection-owned HTTP reader 的 native smoke path：

```
C producer
→ connection-owned bounded FIFO
→ future consumer
```

body FIFO write 現在採 all-or-nothing contract：當可用空間不足以容納整個 pending body span 時回傳 `CK_HTTP_REQUEST_BODY_WRITE_WOULD_BLOCK` 且不前移 `head`、不寫入部分資料。HTTP reader 因而可以在 sink backpressure 時保留同一 pending span，待 consumer 騰出完整空間後再成功 acknowledge，而不重播 body prefix。

這仍是 native bounded consumer primitive／smoke integration，不是 Servlet 6.1 `ServletInputStream`／`ReadListener` implementation。

## 5. Linux io_uring 狀態

io_uring 已被正式納入 Linux platform backend research，但沒有取代 epoll。

目前已持久化：

- `docs/IO_URING_BACKEND_RESEARCH.md`
- `c/event/ck_io_uring_probe.[ch]`
- `tests/event/ck_io_uring_probe_test.c`

設計原則：

- 不使用 liburing。
- 只在 Linux platform backend 使用 `<linux/io_uring.h>` UAPI 與 documented `io_uring_setup/enter/register`。
- 不硬編碼 syscall number。
- runtime probe 實際檢查 ring availability、features 與 opcode support。
- kernel／container／seccomp 不允許時回退 epoll。
- io_uring backend 不改變 HTTP、connection、JNI、ownership、cancellation semantics。
- initial backend 不依賴 SQPOLL、IOPOLL、ZCRX。
- preferred modern target：Linux 6.12+；5.7+ 可作較低相容基線，但必須 runtime probe。
- epoll 不因 io_uring 研究而刪除。
- `ck_io_uring_probe.c` 的 Linux `syscall()` 宣告條件已與 C11/Werror build contract 對齊。

### Kernel capability summary

```
5.1   io_uring core
5.5   IORING_OP_ACCEPT
5.6   IORING_OP_RECV / SEND
5.7   IORING_FEAT_FAST_POLL
5.13  multishot poll
5.19  multishot accept / provided-buffer ring
6.0   multishot recv / SEND_ZC
6.10  recv/send bundle
6.12  incremental provided-buffer-ring capability
6.15  ZCRX / io_uring EPOLL_WAIT
```

kernel version 只是 deployment hint，不是 capability proof。

目前 kernel.org 長期維護線包括 6.18、6.12、6.6、6.1、5.15、5.10；6.18/6.12 的 projected EOL 為 2028-12。

## 6. Thread / event model

目前不預先定案最終 A/B/C JNI topology。

候選仍包括：

- C worker long-lived JNI attachment；
- worker-group bridge；
- central bridge pool。

真正定案需使用 canonical request workload benchmark。

event backend 的 ownership 是 single-owner；registry lock 不得包住 socket I/O。

io_uring 預期採 completion-oriented worker contract；同一 TCP connection 的 receive/send ordering 仍由 connection owner 序列化。

## 7. 驗證狀態原則

GitHub Actions 是最新 CI truth source。

本文件只記錄 gate 類別，不保存歷史 run 清單：

- Linux epoll + loopback TCP：已驗證。
- connection-owned reader/output writer：已驗證。
- keep-alive/recycle：已驗證至目前 executable slice。
- canonical request ABI v2：已驗證至目前 executable smoke slice。
- transactional request body：已實作並有 native unit/integration coverage，需最新 HEAD CI 完整通過後才升級為 verified gate。
- io_uring probe：已實作並有 direct probe test，需最新 HEAD CI 完整通過後才升級為 verified gate。
- ServletInputStream / ReadListener：未完成。
- production multi-worker completion notification：未完成。
- AsyncContext ↔ connection cancellation：未完成。
- Servlet 6.1 TCK：未完成。
- ASan/UBSan/fuzz：未完成。
- production benchmark：未完成。

## 8. Active branch registry

目前 GitHub 報告無 open PR。除 `main` 外的既有分支均視為 historical/provenance branches，不作目前工作基線：

```
codex/bootstrap-jdk21-audit
codex/cgi-interface-research
codex/cgi-nonblocking-completion
codex/completion-contract
codex/completion-routing-impl
codex/core-config-module-research
codex/jni-ownership-abi
codex/multi-request-completion
codex/nonblocking-completion
codex/nonblocking-completion-v2
codex/platform-apache-completion
codex/repo-audit-20260909
codex/rules-cgi-module
codex/servlet-critique-executor-slice
codex/startup-config-loader
codex/web-server-theory-servlet-analysis
codex/win32-research-notification
```

這些 branch 的詳細歷史由各 branch commit graph／closed PR 保存；main 不以其 historical WORK_STATE 覆蓋目前狀態。

## 9. 權威文件入口

平台／事件：
- `docs/EVENT_BACKEND.md`
- `docs/IO_URING_BACKEND_RESEARCH.md`
- `docs/WIN32_LINUX_PLATFORM_RESEARCH.md`

HTTP／connection：
- `docs/HTTP_FRAMING_POLICY.md`
- `docs/HTTP_CONNECTION_READER.md`
- `docs/HTTP_OUTPUT_WRITER.md`
- `docs/HTTP_RESPONSE_STATE.md`
- `docs/CONNECTION_OWNERSHIP.md`

JNI／Java：
- `docs/JNI_ABI.md`
- `docs/JNI_COST_MODEL.md`
- `docs/REQUEST_HANDOFF.md`
- `docs/CANCELLATION_MODEL.md`
- `docs/THREAD_MODEL.md`

架構／證據：
- `docs/ARCHITECTURE.md`
- `docs/HOT_PATH_REVIEW.md`
- `docs/CKARTA_FUNCTION_FLOW.md`
- `docs/FUNCTION_TRACE.md`
- `docs/SECURITY_BASELINE.md`
- `docs/REFERENCE_SOURCES.md`

工作入口：
- `WORKING_RULES.md`：唯一工作守則。
- `docs/WORK_STATE.md`：本文件，僅保存目前有效 state。

## 10. 下一個工程閘門

1. 以最新 `main` HEAD 跑完整 GitHub Actions `make test`，確認 transactional request body 與 io_uring probe gate。
2. 建立 Java `ServletInputStream` 的 minimum semantic adapter，包含 `isReady()`、`ReadListener`、EOF、error、cancel 與 lifecycle。
3. 將 request body lifetime 與 AsyncContext / connection terminal arbitration 接合。
4. 建立 Linux io_uring completion backend prototype，第一階段使用 one-shot ACCEPT/RECV/SEND 與 direct syscalls；與 epoll 保持可切換。
5. 建立 epoll vs io_uring identical-workload benchmark，再決定預設 backend與最低支援 kernel。
6. 之後才進正式 Servlet container routing、TCK、sanitizer/fuzz、TLS 與 end-to-end benchmark。

## 11. Provenance policy

本文件不保存「某日修了什麼、哪個 PR、哪個 CI run 因什麼失敗」的逐條歷史。這些資訊由 Git commit/PR/Actions 保持可追溯性。

只有當歷史事件改變目前仍有效的 invariant 時，才在本文件保留一句現況摘要；完整原因與 diff 應回到相應 commit／專題權威文件。
