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
        └── JNI bridge thread / pool（候選）
                │
                └── Java Servlet executor / container threads
```

這不是「每一 request 一個 native thread」，也不是「所有工作都在單一 event loop」。

前一版曾暫偏向讓所有 C worker 經 central JNI bridge；經 gateway／Servlet／HotSpot 邊界研究與實際 microbenchmark 後，該選擇不再視為先驗決策。正式候選至少包括：

A. C worker 可長期 attach JVM，但只允許執行非 Servlet application code 的 JNI 控制／提交操作；不得在 C event-loop thread 上直接執行 Servlet application code。

B. worker group 對應受控 JNI bridge，避免中央 queue 將所有 request 集中序列化；

C. central bridge thread pool，只有在實測 queue contention（佇列競爭）、CPU locality（CPU 區域性）、Servlet scheduling 或 lifecycle 管理顯示收益時採用。

## 2. Thread 角色

### C main / control thread

負責 process lifecycle、signal／shutdown coordination、global startup state，以及建立／管理其他受控 thread。

不得承擔高併發 connection event loop 工作。

### JVM bootstrap thread

專責 `JNI_CreateJavaVM()` 與啟動期間的 Java bootstrap handoff（啟動交接）。

`JNI_CreateJavaVM()` 的呼叫 thread 會成為 JVM main thread；OpenJDK 21 Invocation API 不建議直接使用 primordial process thread（原始程序執行緒）載入 JVM，而應建立專用 thread。

### C worker thread

每個 worker 擁有自己的 event loop、connection state、request native state 與 request memory pool。

同一 connection 原則上維持單一 owner worker，以減少 shared mutable state（共享可變狀態）。worker 不得共享 `JNIEnv*`。

C worker 是否 attach JVM 作為 JNI submission control path 是正式 benchmark 的比較項目；「attached worker 直接執行 Servlet application」不是合法候選。

### JNI bridge thread

從 C worker queue 取得已完成解析的 request descriptor（請求描述元），執行粗粒度 JNI dispatch，再把 completion record（完成記錄）送回對應 C worker。

bridge 可以是單一 thread、worker-local bridge 或小型 pool。數量不能靠直覺固定，必須測量 queue wait、JNI latency、CPU utilization 與 tail latency（尾端延遲）。

### Java container / executor threads

負責 Servlet、Filter、Listener、application task 與 Java container lifecycle。Java application code 不得在 C event-loop thread 上直接執行。

## 3. JNI attachment 與 worker lifetime

OpenJDK 21 規定 `JNIEnv*` 只對目前 thread 有效；需要操作 JVM 的 native thread 必須 attach，離開前必須 detach。`DestroyJavaVM()` 又會等待 non-daemon threads。

因此 long-lived direct attach（長生命週期直接附加）與 JNI bridge 都必須將 thread lifetime、connection/request ownership、worker restart、failure handling 與 shutdown 一併設計。

不能因為 direct attach 減少 queue 就忽略 worker lifecycle；也不能因為 bridge ownership 較集中就假定其效能較高。

## 4. Request 資料流

attached submission 候選：

```text
C worker event loop
  → HTTP parse complete
  → canonical C request
  → opaque request handle
  → JNI submission/control call on an attached worker
  → Java executor
  → Servlet application code
  → response descriptor
  → C worker event loop
```

bridge 候選：

```text
C worker
  → HTTP parse complete
  → canonical C request
  → opaque request handle
  → bounded JNI queue
  → JNI bridge thread / pool
  → Java request facade
  → Java executor / Servlet
  → response descriptor
  → completion queue
  → C worker event loop
```

兩者均維持 JNI coarse-grained crossing，不在 header byte、body chunk 或單一欄位粒度跨界。

Native buffer 若以 DirectByteBuffer 提供給 Java，只能在明確 borrow interval（借用期間）內使用；C owner 不得在 Java 仍可能讀取時回收或重用該記憶體。

## 5. Queue 與 backpressure

只要採 bridge，C worker → JNI bridge queue 必須 bounded（有界）。

queue 飽和時不能無限配置 request object；應回傳可觀測的 overload state（過載狀態），並依既定 connection／request limit 執行 admission control（准入控制）。

JNI bridge → C worker 的 completion queue 同樣必須有界；completion 無法交付時必須有明確 failure／cancellation policy，不能靜默遺失 request ownership。

attached-submission 路徑若由 C worker 直接呼叫 Java executor 仍必須有 Java executor 的有界資源控制；而 Servlet application code 只能由 Java executor／container thread 執行，不得在 C event-loop thread 上執行。

## 6. Thread affinity 與 JNI

`JNIEnv*` 不得跨 thread 傳遞。推薦保存 process-level `JavaVM*`，需要 JNI 的 native thread 透過當前 thread 的 attach／detach 取得自己的 `JNIEnv*`。

Global reference、local reference、exception state、DirectByteBuffer lifetime 等必須依 JNI 規範管理；不可因 queue 將 JNI local reference 當成可跨 thread／長期保存的 handle。

## 7. shutdown

停止順序必須依所採 topology 明確化。

共同部分：

```text
stop admission
  → stop new C→JNI dispatch
  → drain / cancel outstanding request work
  → complete AsyncContext cancellation
  → stop C event workers
```

若採 bridge：

```text
共同部分
  → drain / cancel JNI queue
  → stop / detach bridge threads
  → terminate Java container
  → DestroyJavaVM
```

若採 attached submission：

```text
共同部分
  → stop Java dispatch from workers
  → ensure no attached worker remains in JNI call
  → detach worker JNI attachments
  → terminate Java container
  → DestroyJavaVM
```

兩種路徑都必須避免「JVM 已 destroy 但 native path 尚可能呼叫 JNI」以及「request ownership 已釋放但 completion 尚會回到 worker」的錯誤。

`DestroyJavaVM()` 不應被視為一般 async cleanup；它是 JVM termination barrier（JVM 終止屏障）。

## 8. 與 Nginx 的交叉比對

固定 Nginx 1.30.4 的 `ngx_master_process_cycle()` 以明確的 master／worker process ownership 管理 worker 啟動、訊號、重配置與停止。其並行單位首先是 process，而不是共享 JVM 的 thread。

Ckarta 不直接採用 Nginx process model，但吸收「owner 明確、lifecycle 明確、控制層負責停止」原則。這使 direct attach 只有在 worker lifetime 本身可控時才合理。

來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/unix/ngx_process_cycle.c

## 9. 與 Tomcat 的交叉比對

固定 Tomcat 11.0.25 的 `NioEndpoint` 將 endpoint、poller、acceptor 與 executor／socket processing 分層。Tomcat 並不是讓所有 network processing 都直接等同於 Servlet application thread。

Ckarta 可吸收角色分離，但不應複製 Tomcat thread count 或 scheduling semantics（排程語意）。

Tomcat 標準 Servlet 留在 JVM；Tomcat `CGIServlet` 則透過 `Runtime.exec()` 進入 JDK native process-creation path。這說明「Java thread」與「OS process」是不同層次的 execution boundary（執行邊界）。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/servlets/CGIServlet.java

## 10. 學術依據

Nickolai Zeldovich、Alexander Yip、Frank Dabek、Robert T. Morris、David Mazières、Frans Kaashoek，"Multiprocessor Support for Event-Driven Programs"，USENIX Annual Technical Conference 2003，pp. 239–252。

用途：支持以 coarse-grained parallelism（粗粒度平行性）及明確 ownership 控制共享可變狀態；不代表 Ckarta 必須複製 libasync-smp。

來源：
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

Rob von Behren、Jeremy Condit、Feng Zhou、George C. Necula、Eric A. Brewer，"Capriccio: scalable threads for internet services"，SOSP 2003，pp. 268–281，DOI 10.1145/945445.945471。

用途：支持 thread-based server（執行緒型伺服器）是可擴展的另一條路徑，因此 direct attach 必須是正式候選而不是被 event model 預先排除。

來源：
https://doi.org/10.1145/945445.945471

Matt Welsh、David Culler、Eric Brewer，"SEDA: An Architecture for Well-Conditioned, Scalable Internet Services"，SOSP 2001，pp. 230–243，DOI 10.1145/502034.502057；ACM SIGOPS 對應文章 DOI 10.1145/502059.502057。

用途：支持 explicit stage、queue、resource control，但不代表 central bridge queue 必然是最佳 placement。

來源：
https://doi.org/10.1145/502034.502057
https://doi.org/10.1145/502059.502057

## 11. OpenJDK 21 證據

Invocation API：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html

JNI functions：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

HotSpot source：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/runtime/javaCalls.cpp

核心約束：

- `JNI_CreateJavaVM()` 的呼叫 thread 成為 JVM main thread。
- primordial thread 不建議直接載入 JVM。
- `JNIEnv*` 僅屬於目前 thread。
- attached native thread 終止前必須 detach。
- `AttachCurrentThread()` 預設是 non-daemon。
- `DestroyJavaVM()` 等待 non-daemon threads 終止。

這些是 thread model 的硬約束，不是 benchmark 可改變的偏好。

## 12. 尚待實測的問題

在正式固定 thread count 與 topology 前，必須 benchmark：

1. C worker attached submission。
2. per-worker／worker-group JNI bridge。
3. central JNI bridge thread pool。
4. 不同 C worker 數與 bridge thread 數的組合。
5. queue wait、JNI crossing latency、Java executor scheduling latency、CPU utilization、tail latency、memory footprint、GC activity。
6. 真實 C request descriptor + opaque handle + DirectByteBuffer + Java facade workload。
7. shutdown、cancellation、AsyncContext、connection ownership。

任何「bridge 比 direct attach 快／慢」的結論，都必須以 Ckarta + OpenJDK 21 可重現 benchmark 證明。


## 13. Servlet compatibility constraint

Java executor thread 必須承擔 Servlet application execution；C event-loop thread 不得因 direct attach 而繞過 executor 直接執行 Servlet application。

這是 Servlet-compatible architecture 的硬邊界，不是效能偏好。attached worker candidate 只可負責 JNI control／submission。
