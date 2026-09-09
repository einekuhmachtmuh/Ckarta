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

- `WORKING_RULES.md`：工程基線、MD 一致性檢查、離線 fallback（替代方法）、工作成果持久化、新工作階段重新讀取、變更衝突／版本一致性、規則衝突處理與最小 wrapper 規則。
- `docs/ENTRYPOINT_DESIGN.md`：C main 與專用 JVM bootstrap thread。
- `docs/STARTUP_STATE_MACHINE.md`：啟動／停止狀態機。
- `docs/CONCURRENCY_MODEL.md`：整體並行原則。
- `docs/THREAD_MODEL.md`：thread model 權威研究文件。
- `docs/THREAD_BENCHMARK_PLAN.md`：thread topology 與 JNI bridge/direct-attach 實驗定義。
- `bench/jni/`：獨立 JNI thread benchmark harness。
- `docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md`：CGI／FastCGI／Tomcat Servlet／CGIServlet／OpenJDK HotSpot／Ckarta JNI 邊界研究。
- `docs/OPENJDK_21U_SOURCE_AUDIT.md`：JDK 21.0.8 與 21.0.11 fixed-tag HotSpot source audit。
- `docs/JNI_ABI.md`：JNI 邊界與 ownership。
- `docs/JNI_COST_MODEL.md`：OpenJDK 21 JNI 成本研究。
- `docs/CONNECTION_OWNERSHIP.md`：C connection、Java facade、buffer lifetime。
- `docs/CANCELLATION_MODEL.md`：跨層取消與資源釋放。

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

OpenJDK 21 HotSpot 的 JNI method invocation 最終進入 `JavaCalls::call` 等 runtime machinery；`NewObjectA/V` 則涉及 instance allocation、JNI handle 與 constructor invocation。因此 C request struct 不應逐欄物件化為大量 Java mirror fields。

詳細權威研究見 `docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md`。

## 5. 本機可重現驗證

Codex 本機測試環境曾成功建置並執行獨立 `bench/jni` harness。測試環境為 Linux x86_64、GCC 14.2.0、OpenJDK 21.0.11。

該 harness 僅作低階 JNI invocation、thread attachment 與 queue handoff 的可執行性／成本拆解工具；先前校驗樣本沒有完整保存 Ckarta commit、kernel、完整硬體隔離、warm-up、repetitions 與 tail-latency 統計，因此本工作狀態不再保存其性能數字，也不將其視為正式 benchmark 證據。

## 6. 尚待完成的 thread 實驗

- repetitions、warm-up、CPU affinity／isolation 控制。
- worker count × bridge count 矩陣。
- p50／p95／p99 histogram。
- long-lived direct attach 的實際 lifecycle。
- JNI invocation、queue wait、Java executor scheduling 的分段量測。
- allocation／GC 與 CPU utilization。
- C canonical request、opaque handle、DirectByteBuffer 與 Java facade workload。
- shutdown、cancellation、AsyncContext 與 connection ownership 的整合測試。

## 7. 下一個工程閘門

1. 以 bounded semantic handoff 為架構基線，完成 Java executor submission 與 completion ownership。
2. 實作 Servlet 6.1 request／response facade 的最小必要邊界；不得讓 C event-loop thread 執行 Servlet application code。
3. 將 AsyncContext lifecycle 接到 C request cancellation／connection ownership。
4. 建立 bounded JNI queue／completion queue 的正式 contract。
5. 將 attached submission、worker-group bridge、central bridge pool 接到相同 canonical request workload。
6. 執行 p50／p95／p99、queue wait、JNI latency、Java scheduling、allocation／GC、CPU utilization 與 memory footprint benchmark。

不得因局部 JNI smoke test、request lifecycle test 或理論分析而宣稱正式 Servlet runtime 或最終 thread topology 已完成。

## 8. 新工作階段接手規則

新工作階段應依序讀取：

1. `WORKING_RULES.md`
2. 本文件
3. `docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md`
4. `docs/OPENJDK_21U_SOURCE_AUDIT.md`
5. `docs/THREAD_MODEL.md`
6. `docs/THREAD_BENCHMARK_PLAN.md`
7. 與當前任務直接相關的架構／JNI／lifecycle 文件
8. 必要時重新核對固定版本 Nginx、Tomcat、OpenJDK 與學術來源

## 9. OpenJDK 21u 版本稽核結果

固定 `jdk-21.0.8-ga` 與 `jdk-21.0.11-ga` 後，核心 `runtime/javaCalls.cpp` blob SHA 相同；`prims/jni.cpp` 的差異未改動本專案依賴的 JNI invocation、object construction、DirectByteBuffer 路徑。權威稽核見 `docs/OPENJDK_21U_SOURCE_AUDIT.md`。

因此 architecture-level JNI path 已可定案；absolute performance 仍須 exact-build benchmark。

## 10. Executable bootstrap／JNI slice

已建立 PR `#1`（branch `codex/bootstrap-jdk21-audit`），包含最小可執行：

- C `main()`。
- JVM bootstrap pthread。
- `JNI_CreateJavaVM()`。
- C worker `AttachCurrentThread()`／`DetachCurrentThread()`。
- `NewDirectByteBuffer()`。
- one Java request facade。
- Makefile、smoke test、GitHub Actions build smoke。

此 slice 只驗證 topology A 的 lifecycle／JNI boundary，不代表 A 為最終 topology。真正 A/B/C benchmark 仍待接上 canonical request workload。

PR #1 已合併至 `main`，merge commit `e501249ce91fd7c76c6625f1826ec0240d4d7d95`；其 executable bootstrap slice 已通過 GitHub Actions smoke build。

## 11. 最新規則與 ABI 進度

2026-09-08：`WORKING_RULES.md` 新增「精簡與整合檢查」及變數／欄位／狀態／handle／buffer reference 的 lifecycle／ownership 檢查，並完成自檢。JNI ownership/cancellation ABI 已進入可執行實作：process-local descriptor、owner/lifetime token、atomic lifecycle state、idempotent cancellation 與 lifecycle test 均已加入；真正 AsyncContext／connection cancellation integration 仍待完成。

## 12. 最新 ABI 進度

PR #2 已於 2026-09-08 squash-merge 至 `main`，merge commit `2638bc5017093b5f6e28478c347c95b77009a8cc`。其 CI 已驗證 request lifecycle ABI、JNI dispatch、DirectByteBuffer 與 shutdown smoke path；下一階段是把此 process-local lifecycle model 接入真正 Servlet AsyncContext／connection ownership。

## 13. Web server theory 結論

2026-09-08：Servlet 6.1、固定 Nginx/Tomcat 原始碼與事件／排隊理論比較後，Ckarta 整體方向維持，但正式架構語意更新為「C event-driven network data plane + bounded semantic handoff + Java Servlet semantic plane」。C event-loop thread 不得執行 Servlet application code；attached worker 只能作 JNI control／submission。完整研究見 `docs/WEB_SERVER_THEORY_SERVLET_NGINX.md`。

## 14. Servlet critique 與 executor progress

2026-09-08：完成 Servlet 6.1 的中立技術批判。結論是保持 Servlet container 身份與高相容性語意，但不讓 Servlet object model 成為全系統 internal representation。新增 `docs/SERVLET_6_1_CRITIQUE.md`。

同日：Java executor handoff 已接入 smoke path，改用固定 1 thread + 有界 queue + AbortPolicy；C 端 completion 取得採非阻塞 poll，但每次 poll 的 attach/detach 仍是 smoke-only 實作，production C event loop 尚不能把此 polling 方式當作最終通知機制。

## 15. CGI／FastCGI 狀態

2026-09-08：完成 CGI/1.1、Nginx FastCGI、PHP-FPM 與 Tomcat CGIServlet 交叉研究。暫定產品方向為：CGI 作為可選外部 application gateway；PHP 優先 FastCGI／PHP-FPM；純 C 可執行程式可經 CGI。尚未實作 process lifecycle、pipe backpressure、reaping、sandbox 或 gateway protocol。

下一個仍待完成的上一階段閘門是 Java executor → 非阻塞 completion → C owner；本次 CGI 研究不取代該閘門。

## 16. CGI/FastCGI 模組化決策

2026-09-08：CGI/FastCGI 正式定位為未來可掛接 application gateway module，不納入核心 request execution。效能研究已把「不存在／存在未命中／實際命中」三種成本分離；學術比較顯示 CGI 的 process creation 是結構性成本，FastCGI 將其移至長生命週期 application process。尚未實作 module loader、CGI process lifecycle 或 FastCGI client。

## 17. 2026-09-08 CGI 模組與非阻塞交接狀態

CGI/FastCGI 已正式定位為未來可掛接 application gateway module，不進核心 request execution；完整效能比較見 `docs/CGI_FASTCGI_RESEARCH.md`。

Java executor handoff 的 v2 smoke slice 使用 bounded Java completion queue，C 不再 Future.get() 阻塞。PR #8 已於 2026-09-08 squash-merge；其 GitHub Actions Build and test 已成功。此 slice 仍只代表單一 in-flight smoke request，不代表正式多請求 completion queue。

## 18. 本輪 CI 錯誤與規則檢查

本輪非阻塞交接實作曾依序發現：20-byte completion record 被錯誤配置成 16 bytes；舊 synchronous dispatch 定義殘留；既有 smoke 腳本要求保留 CKARTA_DISPATCH 診斷輸出。三者均已有現行函式簽名、型態轉換、邊界與全 repository 引用檢查規則涵蓋，因此未新增重複規則；實際改正後 PR #8 的 Build and test 成功。

## 19. 啟動配置研究與實作

2026-09-08：完成 Apache HTTP Server 2.4.68、Nginx 1.30.4、Tomcat 11.0.25 的啟動配置交叉研究。決策為：C main 先載入 native 主設定檔，語法／語意驗證通過後形成 C-owned configuration snapshot，再建立 JVM 與 network runtime；未來 reload 採新快照 prepare→switch→drain。完整研究見 `docs/STARTUP_CONFIGURATION_RESEARCH.md`。

目前已加入最小設定載入器與預設 conf/ckarta.conf；支援 -c、-t、-T、-h 與 class_path 指令。啟動前配置載入已接到 C main → JVM bootstrap，並有 valid/invalid configuration test。此範圍尚未實作一般 network、TLS、worker、module loader 或 reload 設定。

## 20. 多請求 completion 下一閘門

2026-09-09：request_id、owner_token、lifetime_token、overflow、late/duplicate completion、cancellation、shutdown drain 與 owner teardown 的多請求 routing contract 已完成並由 smoke slice 驗證。下一步是把此 contract 接入正式多 worker producer／owner routing，再選擇通知原語；不先綁定 Linux-specific primitive。

## 21. 核心設定與 native module 研究

2026-09-09：完成 Apache HTTP Server、Nginx、Tomcat 的核心設定候選與 native module 架構交叉研究。研究結果持久化於 `docs/CORE_CONFIGURATION_CANDIDATES.md` 與 `docs/MODULE_ARCHITECTURE_RESEARCH.md`；目前不實作新的 core config options 或 module loader。候選分為 P0 程序／路徑／listener、P1 安全／資源／併行、P2 靜態／代理／觀測、P3 平台調校。

Native module 暫定為 load-at-start、ABI/version/signature 驗證、dependency DAG、module-owned configuration、request route pre-resolution；不做 runtime unload。完整 module benchmark 尚未建立。

## 22. 多請求 completion smoke

2026-09-09：多請求 completion routing 已完成第一個可執行 smoke slice。兩個 C request 可在同一 Java executor completion queue 完成，completion 帶 request_id、owner_token、lifetime_token、result、status，C 不依賴完成順序進行 routing。此 slice 仍由 smoke worker 建立後 join，未代表正式 event-loop 非阻塞 producer，也未使用高效率事件通知原語。

## 23. 2026-09-09 exception handling research

完成 Ckarta exception/error handling architecture 研究並建立唯一權威文件 `docs/EXCEPTION_HANDLING_RESEARCH.md`。研究交叉核對 Nginx 1.30.4、Tomcat 11.0.25、Apache HTTP Server 2.4.68、OpenJDK 21 JNI specification，以及 Goodenough 1975、Liskov/Snyder 1979、Lee et al. 2000/2004、Patterson et al. 2002、Candea et al. 2004、Ma et al. 2025 等已核實文獻。

決策：exception、native error、HTTP status、cancellation、timeout 與 fatal state 分層；JNI pending exception 必須在明確邊界檢查與有責任地清除；異步 error/completion/cancellation 必須 exactly once；client-visible error 與 internal diagnostic 分離；retry 不得僅因 exception 觸發。

## 24. 2026-09-09 error state matrix and ck_error decision

依 exception taxonomy 與現有 `ck_request` / completion / cancellation 狀態重新逐狀態分析後，確定需要一個小型 process-local `ck_error_t`，因為單一 `CK_REQUEST_FAILED` 及現有 completion `status` 無法保留 failure source。`docs/ERROR_STATE_MATRIX.md` 已固定 lifecycle／owner／HTTP outcome matrix；已加入獨立 `c/error/ck_error.[ch]` 與 layout/validation test。

`ck_error_t` 已直接內含於 `ck_request_t`，並由 `RUNNING → FAILING → FAILED` publication gate 保護：唯一 failure winner 先寫 error record，再以 release-store 發布 FAILED；`ck_request_error()` 只有在 acquire-load 確認 FAILED 後才回傳該 record。

## 25. 2026-09-09 error record implementation gate

`c/error/ck_error.[ch]` 已建立 process-local error/outcome record，並接入 `ck_request_t`；`tests/error/ck_error_test.c` 與 `tests/error/request_error_race_test.c` 驗證初始化、bounds/flag validation，以及兩個併發 failure publisher 只有一個 winner 並能取得 winner 的 error record。

## 26. 2026-09-09 repository audit

本輪依 `WORKING_RULES.md` 完成全 repository 結構、原始碼、研究文件、測試、CI、設定與固定 upstream gitlink 交叉檢查。已修正：

- JNI DirectByteBuffer descriptor 的 `body`／`body_length` 前置條件，避免把 NULL address 或超過 Java `Integer.MAX_VALUE` 的 capacity 交給 `NewDirectByteBuffer`。
- JVM bootstrap error path 的 mutex／condition lifecycle，並避免持鎖執行 Java container start call。
- `ENTRYPOINT_DESIGN.md` 對 JNI bridge topology 的過時固定描述，重新與 A/B/C benchmark 候選及 `THREAD_MODEL.md` 對齊。
- `THREAD_BENCHMARK_PLAN.md` 與 `bench/jni/README.md` 對目前低階 JNI harness 的能力邊界，移除缺少完整 reproducibility metadata 的性能數字作為證據。
- `WORKING_RULES.md` 文件索引漏列的 gateway／CGI 研究文件，以及 `ARCHITECTURE.md`、`THREAD_BENCHMARK_PLAN.md`、本文件的章節編號／狀態描述失配。

目前仍刻意不定案：

- formal A/B/C thread topology；
- production multi-worker completion notification primitive；
- AsyncContext ↔ C connection cancellation integration；
- production poll／event notification path；
- CGI/FastCGI implementation；
- Servlet 6.1 TCK、ASan/UBSan CI 與正式 performance baseline。

這些仍依既有工程閘門處理，未因本輪 audit 而新增另一套規則。

## 28. 2026-09-09 function flow and error publication

完成 Ckarta 自有函式逐函式流程與契約梳理，持久化於 `docs/CKARTA_FUNCTION_FLOW.md`。已修正：invalid request state 與 FAILED 混淆、runtime shutdown/destroy lifecycle guards、request failure 的 `FAILING → FAILED` publication、Java completion failure taxonomy mapping，以及對應 Makefile dependencies/tests。同步更新 JNI／error matrix／startup 文件，避免 upstream trace 與 Ckarta implementation contract 混在同一權威文件。

目前仍未實作 C network/event backend、HTTP parser、real Servlet container、AsyncContext bridge、production completion notification、正式 public module ABI、Servlet 6.1 TCK、sanitizer/fuzz integration；這些仍是獨立工程閘門。

## 29. 2026-09-09 branch consolidation

`main/WORKING_RULES.md` 現已是唯一工作守則來源；所有非-main branch 的 `WORKING_RULES.md` 均已移除。各 branch 的 `docs/WORK_STATE.md` 只描述該 branch 自身狀態，不覆蓋或取代 main state。

`codex/platform-apache-completion` 的有效研究內容已抽取至 `docs/WIN32_LINUX_PLATFORM_RESEARCH.md` 與 `docs/COMPLETION_NOTIFICATION_RESEARCH.md`。其 `third_party/httpd` submodule 與現行來源模型衝突，因此沒有整枝合併；Apache 2.4.68 僅保留為外部固定研究基線。PR #14 已於 2026-09-09 關閉為 superseded。

## 30. 2026-09-09 branch workflow and function-flow baseline

本輪已完成所有現存非-main branch 的 WORKING_RULES 比對；其仍有效且可泛化的內容已整合到 main/WORKING_RULES.md。非-main branch 不再保存 WORKING_RULES。

新增 `docs/CKARTA_FUNCTION_FLOW.md` 作為 Ckarta 自有實作函式流程與契約的權威文件；`docs/FUNCTION_TRACE.md` 僅保存固定 Nginx/Tomcat upstream function-level trace。

本輪已把 request failure publication 固定為 `RUNNING → FAILING → FAILED`，其中只有成功取得 FAILING 的 publisher 可以寫 error record，再以 release-store 發布 FAILED；reader 以 acquire-load 後讀取 error。

目前仍未實作 C network/event backend、HTTP parser、real Servlet container、AsyncContext bridge、production completion notification、formal public module ABI、Servlet 6.1 TCK 與 sanitizer/fuzz integration。

## 31. 2026-09-09 main-only rules and function-flow consolidation

本輪完成所有現存非-main branch 的 `WORKING_RULES.md` 逐項比對；其仍有效內容已依保留後整合原則合併至 main/WORKING_RULES.md，所有非-main branch 的 `WORKING_RULES.md` 均已移除。非-main `docs/WORK_STATE.md` 仍保留並只描述各自 branch-specific state；沒有以其他 branch 的 WORK_STATE 覆蓋 main。

分支處理：PR #6、#13、#14 已因後續成果取代而關閉；#14 的有效 Win32/Linux 與 completion-notification 研究已抽取至 main，與現行 Apache external fixed-source policy 衝突的 `third_party/httpd` submodule 沒有合併。其餘歷史 branch 仍保留 Git provenance，但不再視為待合併成果；沒有 API 能安全刪除 branch refs 時，不做假刪除。

本輪新增 `docs/CKARTA_FUNCTION_FLOW.md`，統一描述目前 Ckarta 自有可執行函式流程與 ownership/error/lifecycle contract；`docs/FUNCTION_TRACE.md` 僅保存固定 Nginx/Tomcat upstream trace。

`ck_request` failure publication 已固定為 `RUNNING → FAILING → FAILED`，error record 在 FAILING 唯一 winner 中完成寫入，再以 release-store 發布 FAILED；reader 以 acquire-load 後取得 error。`ck_request_finish()` 現為 success-only completion API，避免未攜帶 error record 的 FAILED 路徑。

runtime shutdown 已加入 initialized/started/completed lifecycle guards 與 sequential idempotence；dispatch/poll 在 shutdown 開始後拒絕新操作。同步原語的內部 invariant failure 走明確 fatal path。

目前下一個主要工程閘門仍是 production completion notification / AsyncContext cancellation / event backend；本輪沒有提前定案 Linux epoll/eventfd 或 Windows IOCP 為唯一正式實作，也沒有宣稱完整 Servlet runtime 已完成。

## Branch status registry

此章節是 main 對所有現存非-main branch 的 canonical 狀態索引；每個 branch 自身的 `docs/WORK_STATE.md` 仍是該 branch 的詳細 branch-specific state。

| Branch | Purpose | Lifecycle status | Relation to main |
|---|---|---|---|
| codex/bootstrap-jdk21-audit | JVM bootstrap / OpenJDK 21u / JNI smoke | CLOSED / MERGED | PR #1 merged；僅保留 provenance |
| codex/cgi-interface-research | CGI/FastCGI gateway research | CLOSED / SUPERSEDED | PR #5 closed；有效研究已在 main |
| codex/cgi-nonblocking-completion | early nonblocking completion prototype | CLOSED / SUPERSEDED | 無 active PR；已被後續 completion model 取代 |
| codex/completion-contract | multi-request completion contract research | CLOSED / SUPERSEDED | PR #10 closed；有效語意已在 main |
| codex/completion-routing-impl | completion routing implementation prototype | CLOSED / SUPERSEDED | 無 active PR；後續 smoke/contract 已取代 |
| codex/core-config-module-research | core configuration / native module research | CLOSED / SUPERSEDED | PR #11 closed；研究已在 main |
| codex/jni-ownership-abi | JNI ownership / cancellation ABI | CLOSED / MERGED | PR #2 merged；僅保留 provenance |
| codex/multi-request-completion | multi-request completion routing slice | CLOSED / SUPERSEDED | PR #12 closed；有效 routing contract 已在 main |
| codex/nonblocking-completion | first nonblocking JNI completion slice | CLOSED / SUPERSEDED | PR #6 closed；後續模型已取代 |
| codex/nonblocking-completion-v2 | bounded executor / nonblocking completion v2 | CLOSED / SUPERSEDED | PR #8 closed；有效成果已在 main |
| codex/platform-apache-completion | Win32/Linux + Apache reference/completion research | CLOSED / SUPERSEDED | PR #14 closed；有效研究在 main，httpd submodule 未納入 |
| codex/repo-audit-20260909 | repository audit / exception-error architecture | CLOSED / MERGED | PR #15 merged；僅保留 provenance |
| codex/rules-cgi-module | CGI module/function safety rules | CLOSED / SUPERSEDED | PR #7 closed；有效規則已在 main |
| codex/servlet-critique-executor-slice | Servlet critique / Java executor handoff | CLOSED / SUPERSEDED | PR #4 closed；有效成果已在 main |
| codex/startup-config-loader | startup configuration parser / validation | CLOSED / SUPERSEDED | PR #9 closed；有效成果已在 main |
| codex/web-server-theory-servlet-analysis | Web server / Servlet / Nginx / Tomcat theory | CLOSED / SUPERSEDED | PR #3 closed；研究已在 main |
| codex/win32-research-notification | Win32 / completion notification research | CLOSED / SUPERSEDED | PR #13 closed；有效研究已後續整合 |

No non-main branch currently has an open PR. Branch refs are retained only where useful for historical provenance; they are not active development baselines.

## 32. 2026-09-09 terminal publication protocol

本輪完成 terminal publication 的第一階段 executable contract：`ck_request_cancel()` 回傳 0 表示本次 CAS 取得 CANCELLING ownership、1 表示已有其他 cancellation/failure/completion winner；`ck_request_finish()` 僅允許 RUNNING → COMPLETED；`ck_request_fail()` 使用 RUNNING → FAILING → FAILED publication gate。completion poll 對新成功 terminal 回傳 1、無事件回傳 0、late/duplicate/cancelled completion 被消費但不產生第二 terminal outcome 回傳 2、新發布 failure 回傳 -2，runtime/identity error 回傳負值。

新增 `tests/error/request_terminal_race_test.c`，驗證 cancellation vs completion、failure vs completion 的 terminal ownership race；`tests/error/request_error_race_test.c` 驗證 concurrent failure publisher 只有一個 winner 並保留 winner error record。

學術 correctness 基線新增 Herlihy/Wing linearizability 與 Michael/Scott non-blocking queue references；目前不據此預設採 lock-free completion queue，因為 memory reclamation、overflow、shutdown drain 與 owner lifetime 仍需獨立證明。

目前已完成第一個完整的 executable native completion integration path：Java executor completion → registered JNI publisher → runtime-owned bounded native ring queue → Linux eventfd notification → C epoll wake → native dequeue / request terminal publication。queue 具 close-aware bounded backpressure、overflow/closed semantics、notification coalescing 與 epoll-compatible fd，並有 multi-producer/overflow/drain/close tests。它仍不是完整 Servlet connection data plane 或跨平台最終 backend。

下一個正式閘門改為：owner/lifetime validation 的 production connection integration、queue shutdown drain 的完整 connection semantics、Windows IOCP backend，以及之後的 AsyncContext ↔ C connection cancellation integration。

## 33. 2026-09-09 Tomcat Servlet user compatibility research

新增 `docs/TOMCAT_SERVLET_USER_COMPATIBILITY.md`。研究先搜尋真人作者撰寫的 Tomcat／Servlet 開發筆記、source-reading notes 與技術討論，再與正式 Servlet 6.1 API 及固定 Tomcat 11.0.25 source 交叉核對。文件明確指出真人文章只用於使用者心智模型，不取代規格，也不直接授權修改實作。

研究反覆出現的 user model：Servlet 是 container-managed application component；request 到 Servlet 前必須經 mapping/container chain；Filter 的 `chain.doFilter()` 具有短路與後置處理語意；Servlet instance lifecycle 與 concurrent request invocation 是不同概念；request/response 有 container lifetime；`startAsync()` 可使 request lifetime 超出原 synchronous `service()` invocation；ServletContext/Session/Listener 屬 Java container semantics。

相容性結論：Ckarta 可以改變 network/event-loop/I/O implementation，但不能因此改變 application-visible Servlet lifecycle、mapping、FilterChain、concurrency、async、context/session semantics。真人筆記中使用 Tomcat 私有 class（例如 Valve/Coyote internals）的部分只能作 reference implementation 心智模型，不能直接變成 Ckarta ABI。

## 34. 2026-09-09 native connection ownership slice

新增 `c/connection/ck_connection.[ch]`、`tests/connection/ck_connection_test.c`，並將 connection test 加入 `make test`。native state 為 `OPEN → ASYNC_WAIT → CLOSING → CLOSED`；terminal reason 與 state 以單一 atomic 64-bit lifecycle word 發布，避免 state/reason 分開寫造成 publication race。

terminal reason：COMPLETE、CLIENT_DISCONNECT、TIMEOUT、ERROR、SHUTDOWN。`request_id`、`owner_token`、`lifetime_token` 只作 native correlation/validation，不是 Servlet application ABI。測試驗證 token validation、ASYNC_WAIT transition、completion vs client-disconnect race 與 idempotent close。

本輪亦移除已非空的 `c/connection/.gitkeep` 與 `c/event/.gitkeep`，以符合 WORKING_TREE 對 `.gitkeep` 的用途。

## 35. 2026-09-09 AsyncContext bridge boundary

目前沒有正式 Jakarta Servlet implementation 可供直接接入，故本輪沒有建立假的 `AsyncContext` API 或把 Tomcat 私有 API 變成 Ckarta ABI。下一步應以 Servlet 6.1 API 作 semantics authority、Tomcat 11.0.25 `AsyncContextImpl` 作 reference implementation，逐項實作 Java AsyncContext → native connection event bridge。

候選 mapping：`AsyncContext.complete()` → COMPLETE candidate；`AsyncListener.onTimeout()` → TIMEOUT candidate；`AsyncListener.onError()` → ERROR candidate；client disconnect → CLIENT_DISCONNECT candidate；server shutdown → SHUTDOWN candidate。所有 candidate 必須進入同一 native terminal arbitration，且 Java AsyncContext reference lifetime、native connection lifetime、response ownership 與 recycle invalidation 必須分開證明。

Tomcat 11.0.25 `AsyncContextImpl` 明確處理 complete/timeout/error/onComplete/onError/recycle 與 concurrent-use protection；因此 Ckarta 目前的 connection CAS 是必要的 native ownership primitive，但不是完整 AsyncContext semantics。

## 36. 2026-09-09 validation status

目前 repository 變更已以最新 main head 進行 CI；`c/connection` 測試已接入 `make test`。本輪若 CI 尚未對最新 HEAD 完成，不得把舊 run 當成最新 connection implementation 的通過證據。


## 37. 2026-09-09 Tomcat familiarity cost vs performance decision

本輪確認 docs/TOMCAT_SERVLET_USER_COMPATIBILITY.md 的真正目的為降低既有 Tomcat/Servlet 開發者的 migration／learning cost，而不是複製 Tomcat internal architecture。

工程決策採三層相容性：P0 application-visible Servlet 6.1 semantics；P1 operational familiarity；P2 Tomcat implementation similarity。P0 必須由 Servlet 6.1 規格與 TCK 驗證；P1 優先採熟悉概念但不要求相同 native backend；P2 不作 compatibility requirement。高效能治理不採「全部 C 化」，而採跨界成本預算模型，控制 JNI crossing、crossed bytes、queueing 與 async lifetime memory retention。

此決策與 Nginx/Tomcat 的 abstraction boundary 及 SEDA stage/queue 思路一致：C 主要承擔高 I/O density、connection state 與 network scheduling；Java 保留 Servlet semantic density。不得把 eventfd、epoll、owner token、native connection state 或 JNI queue 暴露給 Servlet application 以降低學習成本。

docs/TOMCAT_SERVLET_USER_COMPATIBILITY.md 為此研究與使用者相容性策略的唯一長篇權威文件；本文件只保存決策與狀態。

## 38. 2026-09-09 Java async semantic core validation

新增 java/org/ckarta/servlet/CkartaAsyncContext.java 與 tests/java/CkartaAsyncContextTest.java，建立正式 Jakarta Servlet API adapter 前的最小 Java async semantic core。它不宣稱實作 jakarta.servlet.AsyncContext，也不把 Tomcat private AsyncContextImpl 當 ABI。

core 目前驗證 start(Runnable)、complete()、container-side timeout/error/disconnect/shutdown terminal injection、listener exactly-once、terminal race 與 recycle invalidation。ACTIVE → COMPLETING|TIMING_OUT|ERRORED 的 CAS 是 terminal ownership 的單一 linearization point。

首版測試曾錯誤把 container-side timeout race 當成 application-facing exception，造成 CI failure；檢查後改為驗證 application complete() 重複呼叫會拒絕，而 internal timeout 輸掉 terminal race 時維持冪等。最新 main CI run 34340207263、job 102428964690、commit 44a785f4196fa6e3a413946f4bcdf74a7f7f3aec 已完整通過 make test。

Makefile 已固定 Jakarta Servlet 6.1 API dependency，並將 Java async semantic test 與 Jakarta API adapter test 納入 test target。完整 Servlet 6.1 API semantics 仍需 request.startAsync、dispatch、timeout、listener cycle、container lifecycle 與 TCK 驗證。

## 39. 2026-09-09 Jakarta Servlet 6.1 API binding prototype

完成第一階段正式 Jakarta API boundary：固定 Maven artifact `jakarta.servlet:jakarta.servlet-api:6.1.0`，SHA-256 `8a31f465f3593bf2351531a5c952014eb839da96a605b5825b93dd54714c48c4`，由 Makefile 下載並驗證後進入 Java compile/test classpath。Eclipse Jakarta Servlet 6.1 release record 明確列出此 Maven 座標與 Java SE 17+ 最低版本；Ckarta 目前以 OpenJDK 21 為基線。

新增 `java/org/ckarta/servlet/CkartaServletAsyncContext.java`，實作 `jakarta.servlet.AsyncContext` 的薄 binding prototype：complete、start、request/response access、timeout、listener registration、listener creation 已連到 Ckarta async semantic core；dispatch 尚明確未實作，並未宣稱 Servlet 6.1 compatibility。`setTimeout(0)` 保持 Servlet 6.1 的 no-timeout semantics；terminal 後 request/response、timeout mutation 與其他 application operations 受 state guard 保護。

新增 `tests/java/CkartaServletAsyncContextTest.java` 驗證 API 型別、request/response identity、timeout、listener completion、createListener 與 dispatch unsupported boundary。依 WORKING_RULES 的保留後整合原則，測試曾出現的 race-sensitive 假設已修正，不新增重複工作規則。

本階段研究與實作重新交叉核對固定 Tomcat 11.0.25 `AsyncContextImpl`：application-facing method 先檢查 state，container-internal terminal path 與 recycle/error protection 分開；Ckarta 只採語意，不複製 Tomcat private class graph。Nginx 1.30.4 event guide 仍作為 native event/posted-event 與非阻塞執行模型的 reference。

目前仍未完成：真正 container request lifecycle 中的 `ServletRequest.startAsync()` integration、`AsyncContext.dispatch()`、完整 `AsyncListener.onStartAsync` cycle、ServletContext/classloader binding、request/response facade、native connection correlation bridge 與 Servlet 6.1 TCK。`CkartaServletRequestAdapter` 已完成獨立 API binding prototype，但不得因此標示相容。

前一個 implementation/test commit `44a785f4196fa6e3a413946f4bcdf74a7f7f3aec` 的完整 `make test` 已通過；其後 API/documentation 與 build wiring 已由最新 HEAD CI 重新驗證。

## 40. 2026-09-09 latest Servlet API binding CI validation

API binding 後續 CI 曾依序抓出並修正：1) async core 舊 `checkUsable()` 引用；2) API test source set 漏列 `CkartaAsyncContext.java`；3) test `main()` 未宣告 `ServletException`；4) API adapter test 對 `createListener()` 的位置假設不理想。這些都屬既有 lifecycle／函式引用／exception contract 規則可處理的問題，沒有新增重複工作規則。

最新 `main` commit `19899e2e31d4bcb0fb07242d5455f0a813045d4e` 的 GitHub Actions run `34341969040`、job `102434414989` 已成功。CI 實際下載 `jakarta.servlet-api-6.1.0.jar`，SHA-256 驗證通過，並成功編譯／執行 Jakarta API adapter test、Java async core test、全部 C unit/race tests 與 native smoke。此結果只證明 API boundary prototype 的 build/test 正確，不等於 Servlet 6.1 TCK 通過。

## 41. 2026-09-09 ServletRequest.startAsync binding validation

新增 `java/org/ckarta/servlet/CkartaServletRequestAdapter.java`，以 `ServletRequestWrapper` 形式建立第一個真正的 `ServletRequest.startAsync()` binding slice。它驗證 `isAsyncSupported()`、同一 dispatch 不得第二次 startAsync、`getAsyncContext()` 僅在 async started 狀態有效，並檢查 supplied request/response 是否為本 dispatch 原始物件或合法 wrapper。`isAsyncStarted()` 在 complete 後恢復 false，但 async cycle consumption 另以獨立狀態追蹤；由於目前尚無 AsyncContext.dispatch implementation，本輪沒有假造新的 dispatch cycle。

`CkartaServletAsyncContext` 目前保持薄 adapter：`complete`、`start`、request/response access、timeout、listener registration、listener creation 已與 semantic core 綁定；dispatch 仍明確 unsupported。native client disconnect／shutdown 不被誤映射為 Servlet `onComplete`。

Servlet 6.1 規格明確要求 `startAsync()` 受 asyncSupported、same-dispatch 與 response closed 等條件限制；Tomcat 11.0.25 `Request`／`RequestFacade` 與 `AsyncContextImpl` 的對應路徑亦將 Request facade、async context 初始化與 container-internal async processing 分開。Ckarta 採語意，不複製 Tomcat private classes。

測試 `tests/java/CkartaServletRequestAsyncTest.java` 已加入 `make test`，驗證 original request/response、合法 wrapper rejection、async state 與 `getAsyncContext()` lifecycle。最新程式碼 commit `efe25c40064cbca3d3e93f1febee94b3571f9ab1` 的 GitHub Actions run `34343191270`、job `102438557460` 已完整通過 `make test`；runner 為 Ubuntu 24.04 / Temurin OpenJDK 21.0.12.1 / GCC 13.3.0。

後續正式 integration gate：將此 adapter 與實際 Servlet request/response facade、Servlet mapping/container lifecycle、native connection correlation、timeout/error dispatch、AsyncListener cycle 與 TCK 逐項接合。不得把本 prototype 標示為 Servlet 6.1 相容。


## 42. 2026-09-09 async cycle identity / native correlation boundary

本輪完成下一個 async bridge primitive：CkartaAsyncCycleBinding 為每一 Servlet async cycle 產生唯一 cycleId，並將 requestId、ownerToken、lifetimeToken 與 cycle identity 綁在同一 Java semantic object 的 lifetime。cycle id 限制在 native connection 目前採用的 48-bit 可表示範圍內；binding 在 terminal publication 後失效。

native ck_connection_t 現已把 cycle id 與 state/terminal event 一併編碼在單一 atomic 64-bit lifecycle word，新增 ck_connection_start_async_cycle() 與 ck_connection_validate_cycle()。這使同一 request/connection 未來在多 async cycle 中不會因只比較 request/owner/lifetime 三個欄位而誤接收 late completion。

completion record 新增 cycle_id；JNI publisher 亦攜帶此欄位。現有 smoke path 暫以 cycle id 1，因正式 Servlet dispatch / 新 async cycle 還未進入 executable path。故這輪已建立 protocol foundation，但 Java binding 與 native connection 尚未透過 production JNI registry/handle 真正互相 lookup；不得宣稱 native correlation bridge 已完成。

研究交叉核對固定 Tomcat 11.0.25 AsyncContextImpl 對 per-cycle state、recycle 與 concurrent use 的處理；Servlet 6.1 API 明確將每次 startAsync 視為可重新初始化的 async cycle，且 AsyncContext 可在 subsequent cycle reused。Nginx development guide 的 connection/event/timer/posted-event 分層則支持把 cycle identity 與 OS event notification 分離。學術 correctness 仍以 Herlihy/Wing linearizability 與 SEDA explicit queue/load conditioning 為基線。

本輪測試另發現並修正 completion record 新增欄位後 test producer 未初始化 cycle_id；此類錯誤可由既有 WORKING_RULES 的變數／欄位生命週期規則直接預防，因此沒有新增工作守則。

下一個正式閘門：建立 native connection registry/opaque binding handle 或等價受控 JNI boundary，使 startAsync 能把 Java cycle binding 真正註冊到正確 native connection owner；其後再做 AsyncContext.complete / timeout / error / client disconnect 的跨層 terminal arbitration。不得把 Java 與 native 各自存在的 correlation objects 視為已完成 bridge。


## 43. 2026-09-09 async cycle / native correlation protocol implementation

本輪在前一階段的 ServletRequest.startAsync binding 基礎上完成 cycle identity protocol foundation。新增 CkartaAsyncCycleBinding，為每個 async cycle 產生唯一 cycleId，並將 requestId、ownerToken、lifetimeToken 與 cycleId 綁在單一 Java semantic binding 的生命週期；terminal publication 後 binding invalidated。cycleId 限制在 native connection 目前使用的 48-bit 表示範圍。

native ck_connection lifecycle 已改為單一 64-bit packed word：低 8 bits 為 connection state、次 8 bits 為 terminal event、高 48 bits 為 async cycle id。新增 ck_connection_start_async_cycle() 與 ck_connection_validate_cycle()；原 ck_connection_start_async() 保留作 compatibility wrapper，使用 cycle id 1 的 smoke default。

completion record 新增 cycle_id，Java JNI publisher 的 native method descriptor 同步改為帶 cycleId；現有 smoke completion 暫固定 cycle id 1，因真正 AsyncContext.dispatch／新 cycle 尚未進入 executable data path。這避免未來同一 request/connection 不同 async cycle 的 late completion 僅依 request/owner/lifetime 三欄而誤命中新 cycle。

Java CkartaServletRequestAdapter 現在建立 cycle binding 並傳入 CkartaAsyncContext；adapter constructor 明確取得 requestId、ownerToken、lifetimeToken。Servlet API 仍只看到 standard AsyncContext，不暴露 native pointer 或 native queue。

重要限制：目前 Java cycle binding 與 native ck_connection_t 仍沒有 production JNI registry/opaque connection handle 進行真正 lookup；因此本輪只能宣稱 correlation identity protocol foundation，不得宣稱 native connection bridge 完成。

本輪 CI 實際抓到的問題包括 completion test 新欄位未初始化、JNI rejection completion 少傳 cycleId，以及兩個 Java test target 漏列 cycle binding source；均依既有 WORKING_RULES 的欄位生命週期、函式簽名與 dependency closure 規則修正，沒有新增規則。

最新 main CI run 34344027099（commit 76e75fead0eb51a3ed2c37e407fe06e02616c748）已開始執行；在此條目建立時尚未取得最終 conclusion，因此不得把它視為通過。前一個 run 34343985342 因 API test target 漏列 CkartaAsyncCycleBinding.java 而失敗，其失敗 log 已核實。

研究交叉核對：Servlet 6.1 AsyncContext API 將每次 startAsync 視為 async cycle，且 repeated startAsync/dispatch semantics 取決於 cycle；Tomcat 11.0.25 AsyncContextImpl 對 per-cycle fields、recycle 與 concurrent access 使用 atomic guard；Nginx development guide 將 connection state、event、timer、posted event 與 event loop 分離；Herlihy/Wing linearizability 作為 terminal ownership correctness baseline；SEDA 作為 bounded explicit queue/load conditioning reference。


## 44. 2026-09-09 strict main-branch audit and CI correction

本次新工作階段以當前 main HEAD `e58be9f51db3063e67d99b83154804af627a817e` 重新讀取 `WORKING_RULES.md`、`docs/WORK_STATE.md` 與規則指定的架構、hot path、function trace、connection ownership 文件，再對目前 main 的 source tree、Makefile、tests、branch/PR 狀態與 GitHub Actions 結果做重新核對。

當前 GitHub Actions run `34344069721`（commit `e58be9f51db3063e67d99b83154804af627a817e`）實際失敗於 `linux-openjdk-21` job 的 `make test`。runner 為 Ubuntu 24.04.5 LTS、Temurin/OpenJDK 21.0.12.1、GCC 13.3.0。失敗原因已由 job log 確認：`JAVA_SERVLET_REQUEST_ASYNC_TEST` target 漏列 `CkartaAsyncCycleBinding.java`，造成 `CkartaServletRequestAdapter`、`CkartaServletAsyncContext`、`CkartaAsyncContext` 與 request async test 的編譯依賴閉包不完整，並同時暴露 test 使用舊 constructor signature 的問題。

本輪直接修正 Makefile 的 source dependency closure，並把 `tests/java/CkartaServletRequestAsyncTest.java` 的 constructor invocation 與目前 `CkartaServletRequestAdapter(ServletRequest, ServletResponse, Executor, TerminalSink, long, long, long)` 完整簽名對齊。此修正沒有新增工作守則；問題可由既有的函式簽名、完整引用、dependency closure 與變數生命週期規則直接處理。

依 `docs/WORKING_TREE.md` 的實際工作樹規則，凡目錄已進入真正程式碼階段即不應保留 `.gitkeep`。本輪因此移除仍位於非空 code/test 目錄的 `c/.gitkeep`、`c/core/.gitkeep`、`c/jni/.gitkeep`、`java/org/ckarta/bootstrap/.gitkeep`、`java/org/ckarta/connector/.gitkeep`、`java/org/ckarta/servlet/.gitkeep`、`tests/java/.gitkeep`；仍為空的規劃目錄保留 `.gitkeep`。

本輪未宣稱本機完整 `make test` 已通過：目前工作環境無法直接以 git clone 建立完整 checkout，因此以 GitHub repository API 與已存在的 CI execution evidence 驗證。修正提交後必須重新檢查新的 main CI conclusion，再決定是否可將本輪驗證標示為通過。

目前仍不變的正式閘門：Servlet 6.1 TCK、production native connection registry/opaque binding、AsyncContext ↔ C connection cancellation、正式 HTTP/network data plane、shutdown drain、Windows IOCP backend、ASan/UBSan 與可重現 performance benchmark。這些不得因本次 build 修正而提前宣稱完成。


## 45. 2026-09-09 native connection registry / JNI async terminal arbitration

本輪依 WORKING_RULES.md 先重新核對 main、WORK_STATE、architecture/hot-path/function/lifecycle/JNI/error 文件，再交叉檢查固定 Nginx 1.30.4、Tomcat 11.0.25 與 Servlet 6.1 AsyncContext semantics。Tomcat AsyncContextImpl 的 application/internal path 分離、per-cycle state、recycle ordering；Nginx 的 connection/event/timer 分層；以及 Herlihy/Wing linearizability、SEDA、Zeldovich event-driven parallelism 文獻，共同支持「native owner 先決定 terminal，Java state 再反映 outcome」的設計；這些來源不是 Ckarta 的形式化證明，也不提供 Ckarta 的效能保證。

新增 c/connection/ck_connection_registry.[ch]：固定容量 256、generation-protected opaque 64-bit handle、registry mutex、active/retire lifecycle。stale handle 在 retire 後失效；slot reuse 以 generation 遞增避免舊 capability 命中新 owner。registry 不把 ck_connection_t *作為 Java application ABI。

新增 c/jni/ck_async_bridge.[ch] 與 package-private CkartaNativeAsyncBridge。JNI method descriptors 已由 C 驅動 JVM integration test 實際註冊與呼叫。Java CkartaAsyncCycleBinding 在建立 cycle 時可註冊 native cycle；CkartaAsyncContext 在有 native capability 時先通過 native terminal gate，再發布自身 local terminal state。

ck_connection_try_terminal() 現明確回傳 0=CLAIMED、1=ALREADY_SAME、2=ALREADY_DIFFERENT；negative value 是 invalid/error。這使 delayed same-event notification 不再與 conflicting terminal event 混同。native different-winner 情況下，Java complete() 不會覆寫 C outcome；相同事件之後才抵達 Java 時可安全反映既有 native terminal outcome。

測試新增 tests/connection/ck_connection_registry_test.c 與 tests/native_async_bridge_smoke.c；前者驗證 capacity、stale handle、generation、identity/cycle 與 retire；後者實際建立 OpenJDK 21 JVM、RegisterNatives、執行 Java ServletRequest.startAsync binding、native terminal claim、conflicting terminal rejection 與 delayed same-event notification。

本輪未宣稱 production AsyncContext bridge 完成。真正 C HTTP connection creation、Servlet request/container injection、response/output ownership、real timeout source、client disconnect event source、async dispatch/new-cycle reinitialization、shutdown drain 與 Servlet 6.1 TCK 仍是後續 gate。JNI bridge 的 capability values 目前只存在 process-local container-internal integration test；正式 product path 尚需由 native connection owner 安全注入。

本輪沒有新增 WORKING_RULES 規則，因所有生命週期、ownership、函式簽名、error、競合與文件一致性要求均可由既有規則直接涵蓋。
