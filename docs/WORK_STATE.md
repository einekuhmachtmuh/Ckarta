# Ckarta 工作現況持久化基線

本文件用於保存跨對話／中斷後仍必須知道的工程現況。它不是取代各專題權威文件的第二套規則；詳細內容仍以對應文件為準。

## 1. 目前 repository 狀態

截至 2026-09-09，`main` 最新提交為當前 GitHub HEAD；本文件目前已隨 gateway／Servlet／native bridge 研究修訂一起提交。

目前重要基線：

- 正式產品程序入口：C `main()`。
- OpenJDK 21 JNI 研究基線：`jdk-21.0.8-ga`。
- Nginx 參考版本：1.30.4，commit `017cf98dcce217946572a896f0992370475e189f`。
- Apache Tomcat 參考版本：11.0.25，commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- 第一階段禁止 JVM 建立後 fork 讓子程序繼承 JVM。
- Apache HTTP Server 2.4.68：tagged commit `736bb657405eb73fd68a64772c3a908807bdb887`，作外部研究基線，不進目前 submodule set。

## 2. 已落實的核心文件

- `WORKING_RULES.md`：工程基線、MD 一致性檢查、離線 fallback、工作成果持久化、新工作階段重新讀取、變更衝突／版本一致性、規則衝突處理與最小 wrapper 規則。
- `docs/ENTRYPOINT_DESIGN.md`
- `docs/STARTUP_STATE_MACHINE.md`
- `docs/CONCURRENCY_MODEL.md`
- `docs/THREAD_MODEL.md`
- `docs/THREAD_BENCHMARK_PLAN.md`
- `docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md`
- `docs/OPENJDK_21U_SOURCE_AUDIT.md`
- `docs/JNI_ABI.md`
- `docs/JNI_COST_MODEL.md`
- `docs/CONNECTION_OWNERSHIP.md`
- `docs/CANCELLATION_MODEL.md`
- `docs/CKARTA_FUNCTION_FLOW.md`
- `docs/COMPLETION_NOTIFICATION_RESEARCH.md`
- `docs/TOMCAT_SERVLET_USER_COMPATIBILITY.md`

## 3. 目前 thread model 決策

第一階段維持可實測候選，而不是先驗固定最終 topology：

```text
C main / control thread
        │
        ├── JVM bootstrap thread
        │       └── JNI_CreateJavaVM()
        │
        ├── C worker threads
        │       └── event loop + connection ownership
        │
        └── JNI bridge thread／pool（候選）
                │
                └── Java Servlet executor threads
```

正式候選至少包括：

A. stable C worker long-lived AttachCurrentThread；
B. worker group 對應受控 JNI bridge；
C. central bridge pool。

目前沒有證據支持把 C 固定到 A、B 或 C 為最終唯一模型。

## 4. Gateway／Servlet／native bridge 研究結論

Classic CGI 的主要邊界是 OS process；Nginx 的實際動態 gateway 主要是 FastCGI 等 upstream protocol，而不是在 Nginx worker 內直接執行一般 CGI。

Tomcat 標準 Servlet 路徑主要留在 JVM／Java thread execution；Tomcat `CGIServlet` 則透過 `Runtime.exec()` 進入 JDK `ProcessImpl` 的 native process-creation path，最後形成 OS-process／stdio 邊界。

Ckarta JNI 與 FastCGI 的主要差別在邊界位置：Ckarta 是 in-process HotSpot/JNI，而 FastCGI 是 protocol/socket/upstream boundary。因此不能因為 FastCGI 的 gateway pattern 有價值，就把 socket／protocol serialization 原封不動放進同程序 JNI。

詳細權威研究見 `docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md`。

## 5. 本機可重現驗證

Codex 本機測試環境曾成功建置並執行獨立 `bench/jni` harness。測試環境為 Linux x86_64、GCC 14.2.0、OpenJDK 21.0.11。

該 harness 僅作低階 JNI invocation、thread attachment 與 queue handoff 的可執行性／成本拆解工具；先前校驗樣本沒有完整保存 Ckarta commit、kernel、完整硬體隔離、warm-up、repetitions 與 tail-latency 統計，因此本工作狀態不保存其性能數字，也不將其視為正式 benchmark 證據。

## 6. 尚待完成的 thread 實驗

- repetitions、warm-up、CPU affinity／isolation 控制。
- worker count × bridge count 矩陣。
- p50／p95／p99 histogram。
- long-lived direct attach 的實際 lifecycle。
- JNI invocation、queue wait、Java executor scheduling 的分段量測。
- allocation／GC 與 CPU utilization。
- C canonical request、opaque handle、DirectByteBuffer 與 Java facade workload。
- shutdown、cancellation、AsyncContext 與 connection ownership 的整合測試。

## 7. 歷史工程閘門與已完成 slice

PR #1、#2、#8 等既有 bootstrap/JNI/completion slices 已進入 main；CGI/FastCGI 仍是獨立未實作 gateway module 方向。

## 8. Branch status registry

此章節是 main 對所有現存非-main branch 的 canonical 狀態索引；每個 branch 自身的 `docs/WORK_STATE.md` 仍是該 branch 的詳細 branch-specific state。所有非-main branch 均沒有 open PR；branch refs 僅保留為 provenance；所有非-main branch 的 `WORKING_RULES.md` 均已移除。

## 9. Tomcat Servlet 使用者相容性研究

2026-09-09：新增 `docs/TOMCAT_SERVLET_USER_COMPATIBILITY.md`。本文件只作使用者心智模型與相容性研究，不直接授權修改 Ckarta 實作。研究以真人 Tomcat／Servlet source-reading notes、正式 Servlet 6.1 API、固定 Tomcat 11.0.25 source 三層交叉驗證。

真人筆記反覆呈現的使用者心智模型包括：Servlet 是 container-managed application component；request 必須經 mapping/container chain 才到 Servlet；FilterChain 可 short-circuit；Servlet instance 與 concurrent request invocation 是不同概念；request/response 有 container lifetime；`startAsync()` 使 application invocation lifetime 與 request lifetime 分離；ServletContext/Session/Listener 屬 Java container semantics。

相容性結論：Ckarta 可以重新設計底層 I/O/event-loop，但不能因 C/JNI 內部架構而改變上述 application-visible Servlet semantics。真人文章只作心智模型證據，不作規格證據；正式判定仍以 Servlet 6.1 與固定 Tomcat source 為準。

## 10. Native connection ownership slice

2026-09-09：新增 `c/connection/ck_connection.[ch]` 與 `tests/connection/ck_connection_test.c`。native connection lifecycle 為：

```text
OPEN → ASYNC_WAIT → CLOSING → CLOSED
```

`ck_connection_try_terminal()` 將 state 與 terminal reason 放在同一 atomic 64-bit lifecycle word，以一次 CAS 發布 terminal ownership，避免先寫 state 再寫 reason 的競態。terminal reason 包括 COMPLETE、CLIENT_DISCONNECT、TIMEOUT、ERROR、SHUTDOWN。

`request_id`、`owner_token`、`lifetime_token` 只作 native correlation/validation identity；它們不取代實際 owner，也不構成 Servlet application ABI。

測試已涵蓋 token validation、ASYNC_WAIT transition、completion vs client-disconnect race 與 idempotent close。非空 `c/connection` 目錄的 obsolete `.gitkeep` 已移除。

## 11. AsyncContext/connection bridge 下一閘門

真正 Servlet 6.1 `AsyncContext` 尚未接入。下一階段以 Tomcat 11.0.25 `AsyncContextImpl` 為 implementation reference、Servlet 6.1 API 為 semantics authority，逐項接通：

- `complete()` → native completion candidate
- `AsyncListener.onTimeout()` → timeout candidate
- `AsyncListener.onError()` → error candidate
- client disconnect → client-disconnect candidate
- server shutdown → shutdown candidate

必須另外證明 Java AsyncContext reference lifetime、native connection owner handoff、timeout/error/disconnect precedence、response ownership、cross-thread cancellation、post-recycle invalidation 與 shutdown drain；不得因現在已有 native CAS 就宣稱 async Servlet 已完成。

## 12. 下一個工程閘門

正式下一個工程 slice 是 AsyncContext ↔ connection ownership integration，以及對應的 Servlet 6.1 compatibility tests；Windows IOCP backend、正式 HTTP data plane、Servlet container hierarchy、TCK、sanitizer/fuzz 与 production benchmark 仍是獨立閘門。
