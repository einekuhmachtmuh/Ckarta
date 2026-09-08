# Ckarta 工作現況持久化基線

本文件用於保存跨對話／中斷後仍必須知道的工程現況。它不是取代各專題權威文件的第二套規則；詳細內容仍以對應文件為準。

## 1. 目前 repository 狀態

截至 2026-09-08，本文件建立時 `main` 最新已核驗提交為：

`c544db787459a765ab2eb98750196af53ada4bb0`

目前重要基線：

- 正式產品程序入口：C `main()`。
- OpenJDK 21 JNI 研究基線：`jdk-21.0.8-ga`。
- Nginx 參考版本：1.30.4，commit `017cf98dcce217946572a896f0992370475e189f`。
- Apache Tomcat 參考版本：11.0.25，commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- 第一階段禁止 JVM 建立後 fork 讓子程序繼承 JVM。

## 2. 已落實的核心文件

- `WORKING_RULES.md`：工程基線、MD 一致性檢查、離線 fallback（替代方法）、工作成果持久化與新工作階段重新讀取規則。
- `docs/ENTRYPOINT_DESIGN.md`：C main 與專用 JVM bootstrap thread。
- `docs/STARTUP_STATE_MACHINE.md`：啟動／停止狀態機。
- `docs/CONCURRENCY_MODEL.md`：整體並行原則。
- `docs/THREAD_MODEL.md`：thread model 權威研究文件。
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

這是目前的工程假設，不是已證明的效能最優解；直接 attach 與 bridge model 的比較仍需 benchmark。

## 4. 最近實測

Codex 本機測試環境：

- Linux x86_64
- GCC 14.2.0
- OpenJDK 21.0.11

已執行最小 JNI thread sanity test：

1. C `main()` 建立專用 pthread。
2. bootstrap pthread 呼叫 `JNI_CreateJavaVM()`。
3. Java 方法確認該 thread 在 JVM 中可執行且為 JVM main thread。
4. 第二個 native pthread 用 `AttachCurrentThread()` 進入 JVM。
5. 該 thread 成功執行 Java 方法。
6. 兩個 native thread 正常 detach。
7. `DestroyJavaVM()` 回傳 `JNI_OK`。

注意：本次環境沒有掛載 Ckarta repository 工作樹，因此這不是 Ckarta build/test 結果，也不能冒充 Ckarta 已編譯通過。測試使用的 JDK 21.0.11 也不是文件鎖定的 `jdk-21.0.8-ga` 研究基線；它只用來驗證 JNI thread lifecycle 的基本可行性。

## 5. 下一個工程閘門

在真正建立 C entrypoint source 前，優先完成：

- Ckarta build system 最小可編譯切片。
- JVM bootstrap thread 的 ready/error handoff。
- JNI bridge queue 的 ownership 與 cancellation ABI。
- Java bootstrap API 的最小公開邊界。
- bridge thread 數量與 direct-attach alternative 的 benchmark harness。
- shutdown／join 的可執行測試。

不得因「thread model 已有文件」而宣稱上述項目已實作。

## 6. 新工作階段接手規則

新工作階段應依序讀取：

1. `WORKING_RULES.md`
2. 本文件
3. `docs/THREAD_MODEL.md`
4. 與當前任務直接相關的架構／JNI／lifecycle 文件
5. 必要時重新核對固定版本 Nginx、Tomcat、OpenJDK 與學術來源

任何只存在聊天上下文、尚未進 repository 的重要決策，不應視為已持久化工程狀態。
