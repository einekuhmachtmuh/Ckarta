# Ckarta 並行模型

本文件描述整體並行原則；具體 thread roles（執行緒角色）、JNI attachment、queue 與 shutdown 約束以 `docs/THREAD_MODEL.md` 為權威。

## 1. 基本模型

第一階段：

one JVM process（單一 JVM 程序）
+
C event worker threads（C 事件工作者執行緒）
+
JNI semantic handoff（JNI 語意交接；attached submission 或 bridge）
+
Java Servlet executor threads（Java Servlet 執行器執行緒）
+
JVM bootstrap thread（JVM 啟動執行緒）

這不是「所有工作都在事件迴圈」，也不是「每一 request 一個 native thread」。

## 2. C

C worker 擁有：

- event loop
- connection state
- request native state
- native memory pool

同一 connection 原則上由固定 owner worker 推進。

worker 間避免共享可變 connection state。

C main／control thread 擁有 process lifecycle；bootstrap thread 負責 `JNI_CreateJavaVM()` 及其明確的 JVM startup／handoff（啟動／交接）責任。

## 3. Java

Java executor 執行：

- Servlet
- Filter
- Listener callbacks（監聽器回呼）
- application task

C event loop 不得直接執行可能長時間阻塞的 Servlet application code。

## 4. JNI

JNI crossing（JNI 邊界穿越）應在 request lifecycle 的粗粒度階段：

C parse complete
→ descriptor
→ bounded semantic handoff
→ Java executor
→ Servlet processing
→ response descriptor
→ completion owner
→ C worker

而非：

header byte
→ Java
→ next header byte
→ Java

`JNIEnv*` 是 thread-local（執行緒區域）介面；不得在 C workers 或 bridge threads 之間共享。每個需要 JVM 存取的 native thread 都必須遵守自身 attach／detach 生命週期。

第一階段不把「worker 是否 attach JVM」與「Servlet application 執行在哪個 thread」混為一談；後者固定由 Java executor／container thread 負責。前者的取捨見 `docs/THREAD_MODEL.md`。

## 5. Worker ownership

使用 worker ownership 主要是降低 shared mutable state（共享可變狀態）。

這與 Zeldovich 等人的事件驅動多處理器研究相符：可將沒有共享可變狀態的工作單元以較粗粒度方式平行化，而避免不必要的 fine-grained synchronization（細粒度同步）。

來源：

https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

## 6. SEDA

Ckarta 不宣稱是 SEDA。

可吸收：

- explicit stage boundaries（明確階段邊界）
- bounded queues（有界佇列）
- overload control（過載控制）

學術來源：

Matt Welsh、David Culler、Eric Brewer，
"SEDA: An Architecture for Well-Conditioned, Scalable Internet Services"

DOI：
https://doi.org/10.1145/502059.502057

## 7. Shared state

預設避免：

global request table
global lock
global cache mutation

除非 profiling（效能分析）證明必要。

## 8. Lock-free

不預設。

如果未來採用 lock-free data structure（無鎖資料結構），必須同時證明：

- contention
- memory reclamation
- linearizability
- benchmark benefit

## 9. Java與C的平行化界線

C event worker：
只推進 native state machine（原生狀態機）與 connection ownership。

JNI bridge：
只負責受控的粗粒度 native↔JVM dispatch，不取得 connection ownership。

Java executor：
推進 application-visible Servlet semantics（應用程式可見 Servlet 語意）。

跨界事件：
completion record（完成記錄）。

JVM bootstrap thread：
只在啟動／停止協定需要時進行 JVM lifecycle coordination，不取代 C worker 的 network data-plane ownership。

## 10. 關閉

shutdown 時：

停止新 dispatch
→ 停止／排空 JNI queue
→ 完成／取消 Java task
→ 完成 async cancellation
→ 關閉 C connections
→ 停止 C workers
→ detach remaining JNI-attached native threads
→ terminate Java container
→ DestroyJavaVM
→ join bootstrap／bridge／worker threads
→ native cleanup

OpenJDK 21 Invocation API 規定 `DestroyJavaVM()` 會等待 non-daemon threads，因此 shutdown 必須先使 JNI-attached thread 的生命週期可控。

詳細停止順序與競態案例見 `docs/THREAD_MODEL.md`、`docs/CANCELLATION_MODEL.md`。

來源：

https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html


## 11. Servlet 6.1 implementation freedom

Servlet 6.1 的 externally visible semantics 與 internal scheduling strategy 分離。Ckarta 可用 C event-driven scheduling 提供 input/output readiness，再由 Java executor 執行 Servlet application。

不得為符合規格而把 HTTP socket readiness 或 native connection state 暴露給 Servlet application。
