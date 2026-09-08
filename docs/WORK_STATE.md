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

依優先序：

1. 最小 Ckarta build system。
2. 真正 C `main()` 與 bootstrap thread。
3. ready/error handoff。
4. JNI boundary 的正式 ownership／cancellation ABI。
5. Java bootstrap API 最小公開邊界。
6. shutdown／join 可執行測試。
7. 再把 thread benchmark 接到真實 Ckarta request path。

不得因 benchmark harness 或研究文件已存在而宣稱正式 Ckarta runtime 已實作。

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

## 9. 本次 OpenJDK 21u 版本稽核結果

固定 `jdk-21.0.8-ga` 與 `jdk-21.0.11-ga` 後，核心 `runtime/javaCalls.cpp` blob SHA 相同，確認 JNI method invocation 所依賴的 `JavaCalls::call` machinery 在這兩個 update 間沒有變更。

`prims/jni.cpp` 的 compare 只有 6 行 change：copyright 年份及一處不可達 lint-noise return；未改動本專案所依賴的 JNI invocation、object construction、DirectByteBuffer 相關路徑。

因此「OpenJDK update 版本不同導致 architecture claim 無法定案」可以解除；現在僅剩 implementation-specific absolute performance 必須以實際 exact build benchmark 定量。

權威稽核見 `docs/OPENJDK_21U_SOURCE_AUDIT.md`。

## 10. 本次 executable vertical slice

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
