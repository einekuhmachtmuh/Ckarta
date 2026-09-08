# Ckarta 工作現況持久化基線

本文件用於保存跨對話／中斷後仍必須知道的工程現況。它不是取代各專題權威文件的第二套規則；詳細內容仍以對應文件為準。

## 1. 目前 repository 狀態

截至 2026-09-08，`main` 最新提交應以 GitHub 為準；本文件目前已隨 thread benchmark 與文件一致性修訂一起提交。

目前重要基線：

- 正式產品程序入口：C `main()`。
- OpenJDK 21 JNI 研究基線：`jdk-21.0.8-ga`。
- Nginx 參考版本：1.30.4，commit `017cf98dcce217946572a896f0992370475e189f`。
- Apache Tomcat 參考版本：11.0.25，commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- 第一階段禁止 JVM 建立後 fork 讓子程序繼承 JVM。

## 2. 已落實的核心文件

- `WORKING_RULES.md`：工程基線、MD 一致性檢查、離線 fallback（替代方法）、工作成果持久化、新工作階段重新讀取，以及變更衝突／版本一致性檢查規則。
- `docs/ENTRYPOINT_DESIGN.md`：C main 與專用 JVM bootstrap thread。
- `docs/STARTUP_STATE_MACHINE.md`：啟動／停止狀態機。
- `docs/CONCURRENCY_MODEL.md`：整體並行原則。
- `docs/THREAD_MODEL.md`：thread model 權威研究文件。
- `docs/THREAD_BENCHMARK_PLAN.md`：thread topology 與 JNI bridge/direct-attach 實驗定義。
- `bench/jni/`：獨立 JNI thread benchmark harness。
- `docs/JNI_ABI.md`：JNI 邊界與 ownership。
- `docs/JNI_COST_MODEL.md`：OpenJDK 21 JNI 成本研究。
- `docs/CONNECTION_OWNERSHIP.md`：C connection、Java facade、buffer lifetime。
- `docs/CANCELLATION_MODEL.md`：跨層取消與資源釋放。

## 3. 目前 thread model 決策

第一階段暫定：

```text
C main / control thread
        │
        ├── JVM bootstrap thread
        │       └── JNI_CreateJavaVM()
        │
        ├── C worker threads
        │       └── event loop + connection ownership
        │
        └── JNI bridge thread／pool
                │
                └── Java Servlet executor threads
```

第一階段不把所有 C workers 固定 attach JVM。C worker 將已完成解析的 request descriptor 放入 bounded JNI queue，由 JNI bridge 執行粗粒度 JNI dispatch，再以 completion record 回到原 owner worker。

這仍是工程假設，不是已證明的效能最優解。direct attach 與 bridge model 的差異必須由 benchmark 決定。

## 4. 本機可重現驗證

Codex 本機測試環境：

- Linux x86_64
- GCC 14.2.0
- OpenJDK 21.0.11

已建立並建置獨立 `bench/jni` harness：

```text
make
./build/ckarta-jni-thread-bench direct 2 10000
./build/ckarta-jni-thread-bench bridge 2 10000 1
./build/ckarta-jni-thread-bench bridge 2 10000 2
```

一次校驗樣本：

```text
direct 2/10000：throughput 約 6.01 M ops/s，avg operation 約 110 ns
bridge 2/10000/1：throughput 約 0.104 M ops/s，avg operation 約 13.2 us，avg queue wait 約 6.39 us
bridge 2/10000/2：throughput 約 0.0419 M ops/s，avg operation 約 36.4 us，avg queue wait 約 21.1 us
```

這些數字只證明目前 harness 能運作並顯示 queue／serialization overhead（佇列／序列化額外成本）可能非常顯著；它們不是 Ckarta 整體效能結論，也不是固定 OpenJDK 21.0.8 的 benchmark 基線。

## 5. 尚待完成的 thread 實驗

- repetitions 與 warm-up 控制。
- CPU affinity／isolation（CPU 親和／隔離）控制。
- worker count × bridge count 矩陣。
- p50／p95／p99 histogram。
- direct attach 的長生命週期 attached worker 模式。
- JNI invocation、queue wait、Java executor scheduling 的分段量測。
- allocation／GC 與 CPU utilization。
- 將 workload 從 primitive `long` 擴展到 C canonical request、opaque handle 與 DirectByteBuffer。
- shutdown、cancellation 與 connection ownership 的整合測試。

## 6. 下一個工程閘門

依優先序：

1. 最小 Ckarta build system。
2. 真正 C `main()` 與 bootstrap thread。
3. ready/error handoff。
4. JNI bridge queue 的正式 ownership／cancellation ABI。
5. Java bootstrap API 最小公開邊界。
6. shutdown／join 可執行測試。
7. 再把 thread benchmark 接到真實 Ckarta request path。

不得因 benchmark harness 已存在而宣稱正式 Ckarta runtime 已實作。

## 7. 新工作階段接手規則

新工作階段應依序讀取：

1. `WORKING_RULES.md`
2. 本文件
3. `docs/THREAD_MODEL.md`
4. `docs/THREAD_BENCHMARK_PLAN.md`
5. 與當前任務直接相關的架構／JNI／lifecycle 文件
6. 必要時重新核對固定版本 Nginx、Tomcat、OpenJDK 與學術來源

任何只存在聊天上下文、尚未進 repository 的重要決策，不應視為已持久化工程狀態。
