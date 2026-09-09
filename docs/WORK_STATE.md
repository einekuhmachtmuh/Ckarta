# Ckarta 工作現況持久化基線

本文件用於保存跨對話／中斷後仍必須知道的工程現況。它不是取代各專題權威文件的第二套規則；詳細內容仍以對應文件為準。

## 1. 目前 repository 狀態

截至 2026-09-08，`main` 最新提交應以 GitHub 為準；本文件目前已隨 gateway／Servlet／native bridge 研究修訂一起提交。

目前重要基線：

- 正式產品程序入口：C `main()`。
- OpenJDK 21 JNI 研究基線：`jdk-21.0.8-ga`。
- Nginx 參考版本：1.30.4，commit `017cf98dcce217946572a896f0992370475e189f`。
- Apache Tomcat 參考版本：11.0.25，commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- 第一階段禁止 JVM 建立後 fork 讓子程序繼承 JVM。

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

Codex 本機測試環境：

- Linux x86_64
- GCC 14.2.0
- OpenJDK 21.0.11

已建立並建置獨立 `bench/jni` harness。一次校驗樣本顯示 direct attach 在極小 workload 下明顯避免了 central queue handoff；bridge／bridge pool 則出現顯著 queue／serialization overhead。

這些數字只用來驗證 harness 與成本拆解方向，不是 Ckarta 整體效能結論，也不是固定 OpenJDK 21.0.8 的正式 benchmark。

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

## 11. 最新 ABI 進度

PR #2 已於 2026-09-08 squash-merge 至 `main`，merge commit `2638bc5017093b5f6e28478c347c95b77009a8cc`。其 CI 已驗證 request lifecycle ABI、JNI dispatch、DirectByteBuffer 與 shutdown smoke path；下一階段是把此 process-local lifecycle model 接入真正 Servlet AsyncContext／connection ownership。

## 12. Web server theory 結論

2026-09-08：Servlet 6.1、固定 Nginx/Tomcat 原始碼與事件／排隊理論比較後，Ckarta 整體方向維持，但正式架構語意更新為「C event-driven network data plane + bounded semantic handoff + Java Servlet semantic plane」。C event-loop thread 不得執行 Servlet application code；attached worker 只能作 JNI control／submission。完整研究見 `docs/WEB_SERVER_THEORY_SERVLET_NGINX.md`。


## 13. Servlet critique 與 executor progress

2026-09-08：完成 Servlet 6.1 的中立技術批判。結論是保持 Servlet container 身份與高相容性語意，但不讓 Servlet object model 成為全系統 internal representation。新增 docs/SERVLET_6_1_CRITIQUE.md。

同日：Java executor handoff 已接入 smoke path，改用固定 1 thread + 有界 queue + AbortPolicy；同步等待僅供 smoke 驗證，production C event loop 尚不能使用此 blocking completion 方式。
## 15. CGI／FastCGI 狀態

2026-09-08：完成 CGI/1.1、Nginx FastCGI、PHP-FPM 與 Tomcat CGIServlet 交叉研究。暫定產品方向為：CGI 作為可選外部 application gateway；PHP 優先 FastCGI／PHP-FPM；純 C 可執行程式可經 CGI。尚未實作 process lifecycle、pipe backpressure、reaping、sandbox 或 gateway protocol。

下一個仍待完成的上一階段閘門是 Java executor → 非阻塞 completion → C owner；本次 CGI 研究不取代該閘門。

## 16. CGI/FastCGI 模組化決策

2026-09-08：CGI/FastCGI 正式定位為未來可掛接 application gateway module，不納入核心 request execution。效能研究已把「不存在／存在未命中／實際命中」三種成本分離；學術比較顯示 CGI 的 process creation 是結構性成本，FastCGI 將其移至長生命週期 application process。尚未實作 module loader、CGI process lifecycle 或 FastCGI client。

## 17. 2026-09-08 CGI 模組與非阻塞交接狀態

CGI/FastCGI 已正式定位為未來可掛接 application gateway module，不進核心 request execution；完整效能比較見 docs/CGI_FASTCGI_RESEARCH.md。

Java executor handoff 的 v2 smoke slice 使用 bounded Java completion queue，C 不再 Future.get() 阻塞。PR #8 的最新 CI 正在／尚待最終驗證；在 CI 成功前不得視為主線完成。


## Branch status

- Purpose: bounded Java executor + nonblocking completion v2
- Lifecycle status: CLOSED / SUPERSEDED
- Relation to main: PR #8 closed;後續 main 已吸收其有效成果。
- This file is branch-specific state and must not be treated as main canonical state.
