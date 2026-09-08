# Ckarta Thread 模型基線

本文件是 Ckarta thread model（執行緒模型）的目前權威研究文件。它描述角色、ownership（所有權）、JNI attachment、queue 邊界與 shutdown 約束；不代表所有元件已實作。

## 1. 第一階段模型

Ckarta 第一階段採單一 JVM process：

```text
C main / control thread
        │
        ├── JVM bootstrap thread
        │       └── JNI_CreateJavaVM()
        │
        ├── C worker threads
        │       └── event loop + connection ownership
        │
        └── JNI bridge thread pool（初步可先 1 個，之後以 benchmark 決定）
                │
                └── Java Servlet executor / container threads
```

這不是「每一 request 一個 native thread」，也不是「所有工作都在單一 event loop」。

第一階段暫不讓 C event worker 直接成為固定的 JNI-attached thread；跨 JVM 的工作先經由受控 JNI bridge（JNI 橋接執行緒）完成。這是降低 `JNIEnv*` thread affinity（執行緒親和性）、native thread lifetime 與 `DestroyJavaVM()` 耦合的暫定工程決策，必須以後續 benchmark 驗證其額外 queue／context-switch 成本。

## 2. Thread 角色

### C main / control thread

負責 process lifecycle、signal／shutdown coordination、global startup state，以及建立／管理其他受控 thread。

不得承擔高併發 connection event loop 工作。

### JVM bootstrap thread

專責 `JNI_CreateJavaVM()` 與啟動期間的 Java bootstrap handoff（啟動交接）。

`JNI_CreateJavaVM()` 的呼叫 thread 會成為 JVM main thread；OpenJDK 21 Invocation API 並強烈建議不要直接使用 primordial process thread（原始程序執行緒）載入 JVM，而應建立專用 thread。

### C worker thread

每個 worker 擁有自己的 event loop、connection state、request native state 與 request memory pool。

同一 connection 原則上維持單一 owner worker，以減少 shared mutable state（共享可變狀態）。worker 不直接共用 `JNIEnv*`。

### JNI bridge thread

從 C worker queue 取得已完成解析的 request descriptor（請求描述元），執行粗粒度 JNI dispatch，再把 completion record（完成記錄）送回對應 C worker。

它可以是單一 thread 或小型 thread pool；是否擴大數量不能靠直覺決定，必須測量 queue wait、JNI latency、CPU utilization 與 tail latency（尾端延遲）。

### Java container / executor threads

負責 Servlet、Filter、Listener、application task 與 Java container lifecycle。Java application code 不得在 C event-loop thread 上直接執行。

## 3. 為什麼暫不讓 C worker 全部 attach JVM

OpenJDK 21 規定 `JNIEnv*` 只對目前 thread 有效；需要操作 JVM 的其他 native thread 必須 attach，離開前必須 detach。`DestroyJavaVM()` 又會等待 non-daemon threads，而 `AttachCurrentThread()` 預設建立 non-daemon native thread。

因此「每一 C worker 永久 attach」雖然可以減少一層 queue，但會把 JVM thread lifecycle（JVM 執行緒生命週期）直接綁到 network worker lifetime（網路工作者生命週期），使 shutdown、worker respawn 與資源回收更複雜。

這不是證明 dedicated bridge 一定更快；它只是一個目前較容易驗證 ownership 與 shutdown 的初步模型。

## 4. Request 資料流

```text
C worker
  → HTTP parse complete
  → canonical C request
  → opaque request handle
  → bounded JNI queue
  → JNI bridge thread
  → Java request facade
  → Java executor / Servlet
  → response descriptor
  → JNI bridge
  → C worker completion queue
  → connection event loop
```

JNI crossing 保持粗粒度，不在 header byte、body chunk 或單一欄位粒度跨界。

Native buffer 若以 DirectByteBuffer 提供給 Java，只能在明確 borrow interval（借用期間）內使用；C owner 不得在 Java 仍可能讀取時回收或重用該記憶體。

## 5. Queue 與 backpressure

C worker → JNI bridge 的 queue 必須 bounded（有界）。

queue 飽和時不能無限配置 request object；應回傳可觀測的 overload state（過載狀態），並依既定 connection／request limit 執行 admission control（准入控制）。

JNI bridge → C worker 的 completion queue 同樣必須有界；completion 無法交付時必須有明確 failure／cancellation policy，不能靜默遺失 request ownership。

## 6. Thread affinity 與 JNI

`JNIEnv*` 不得跨 thread 傳遞。

推薦保存 `JavaVM*` 作為 process-level handle；需要 JNI 的 bridge thread 透過當前 thread 的 attach／detach 取得自己的 `JNIEnv*`。

Global reference、local reference、exception state、DirectByteBuffer lifetime 等必須依 JNI 規範管理；不可因 queue 將 JNI local reference 當成可跨 thread／長期保存的 handle。

## 7. shutdown

停止順序暫定：

```text
stop admission
  → stop new C→JNI dispatch
  → drain / cancel JNI queue
  → drain / cancel Java task
  → complete AsyncContext cancellation
  → stop C event workers
  → detach remaining JNI-attached native threads
  → terminate Java container
  → DestroyJavaVM
  → join controlled threads
  → native cleanup
```

真正實作時必須避免「C worker 已停止但 queue 中仍有需要它回收的 request」以及「JVM 已 destroy 但 native completion path 仍可能呼叫 JNI」兩種生命週期錯誤。

`DestroyJavaVM()` 不應被視為一般 async cleanup；它是 JVM termination barrier（JVM 終止屏障）。

## 8. 與 Nginx 的交叉比對

固定 Nginx 1.30.4 的 `ngx_master_process_cycle()` 以明確的 master／worker process ownership 管理 worker 啟動、訊號、重配置與停止；worker 不需要共享同一個 Java VM thread-local interface。Ckarta 可吸收其「owner 明確、lifecycle 明確、停止由控制層協調」的原則，但不直接採用 Nginx 的 process model。

來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/unix/ngx_process_cycle.c

## 9. 與 Tomcat 的交叉比對

固定 Tomcat 11.0.25 的 NioEndpoint 將 network endpoint、polling 與 Java executor／Acceptor 等責任分層；Tomcat 本身也以 Java thread／executor model 執行 protocol processing。Ckarta 的差異在於 network data plane 由 C worker ownership 管理，再透過 JNI bridge 接入 Java Servlet executor。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java

## 10. 學術依據

Nickolai Zeldovich、Alexander Yip、Frank Dabek、Robert T. Morris、David Mazières、Frans Kaashoek，"Multiprocessor Support for Event-Driven Programs"，USENIX Annual Technical Conference 2003，pp. 239–252。

用途：支持以 coarse-grained parallelism（粗粒度平行性）以及對共享可變狀態的明確控制，讓 event-driven systems 擴展到多核心；不代表 Ckarta 必須複製 libasync-smp。

來源：
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

Rob von Behren、Jeremy Condit、Feng Zhou、George C. Necula、Eric Brewer，"Capriccio: Scalable Threads for Internet Services"，Proceedings of the 19th ACM Symposium on Operating Systems Principles (SOSP 2003), pp. 268–281，DOI 10.1145/945469.945471。

用途：提醒 thread-based server（執行緒型伺服器）可以提供較直觀的 programming model（程式設計模型）與高併發擴展性；因此 Ckarta 不應把 event-driven 與 threads 視為互斥信仰，而應依工作負載與 ownership 選擇。

來源：
https://doi.org/10.1145/945469.945471

## 11. OpenJDK 21 證據

Invocation API：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html

核心約束：

- `JNI_CreateJavaVM()` 的呼叫 thread 成為 JVM main thread。
- primordial thread 不建議直接載入 JVM。
- `JNIEnv*` 僅屬於目前 thread。
- attached native thread 終止前必須 detach。
- `AttachCurrentThread()` 預設是 non-daemon。
- `DestroyJavaVM()` 等待 non-daemon threads 終止。

這些是 thread model 的硬約束，不是 benchmark 可改變的偏好。

## 12. 尚待實測的問題

在正式固定 thread count 前，必須 benchmark：

1. C worker 直接 attach + JNI dispatch。
2. 單一 JNI bridge thread + bounded queue。
3. 小型 JNI bridge pool + bounded queue。
4. 不同 C worker 數與 bridge thread 數的組合。
5. queue wait、JNI crossing latency、Servlet scheduling latency、CPU utilization、tail latency、memory footprint、GC activity。

任何「bridge thread 比直接 attach 快／慢」的結論都必須以 OpenJDK 21 與 Ckarta 實測證明。
