# Ckarta 並行模型

## 1. 基本模型

第一階段：

one JVM process（單一 JVM 程序）
+
C event worker threads（C 事件工作者執行緒）
+
Java Servlet executor threads（Java Servlet 執行器執行緒）

這不是「所有工作都在事件迴圈」。

## 2. C

C worker 擁有：

- event loop
- connection state
- request native state
- native memory pool

同一 connection 原則上由固定 owner worker 推進。

worker 間避免共享可變 connection state。

## 3. Java

Java executor 執行：

- Servlet
- Filter
- Listener callbacks（監聽器回呼）
- application task

C event loop 不得直接呼叫會長時間阻塞的 Servlet application code。

## 4. JNI

JNI crossing（JNI 邊界穿越）應在 request lifecycle 的粗粒度階段：

C parse complete
→ descriptor
→ Java processing
→ response descriptor

而非：

header byte
→ Java
→ next header byte
→ Java

## 5. Worker ownership

使用 worker ownership 主要是降低 shared mutable state（共享可變狀態）。

這與 Zeldovich 等人的事件驅動多處理器研究相符：平行化的關鍵之一是找出不共享可變狀態的工作單元。

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
只推進 native state machine（原生狀態機）。

Java executor：
推進 application-visible Servlet semantics（應用程式可見 Servlet 語意）。

跨界事件：
completion record（完成記錄）。

## 10. 關閉

shutdown 時：

停止新 dispatch
→ 等待／取消 Java task
→ 完成 async cancellation
→ 關閉 C connection
→ 結束 C worker
→ JVM shutdown
