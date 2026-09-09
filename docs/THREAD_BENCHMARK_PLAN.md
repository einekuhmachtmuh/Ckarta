# Ckarta Thread／JNI Benchmark 計畫

本文件定義 thread topology（執行緒拓撲）與 JNI crossing 成本的可重現實驗。它不預先指定哪一種模型較快。

## 1. 目的

比較：

1. C worker AttachCurrentThread() 後只執行 JNI control／submission，Servlet application code 由 Java executor 執行。
2. C worker 將 operation 放入 bounded JNI queue，由固定 JNI bridge thread 執行 Java invocation。
3. 小型 JNI bridge thread pool。

目標是分離並量測：JNI crossing、thread attachment、queue wait、scheduling、context handoff 與 Java invocation 的成本。

## 2. 最小 workload

Java target method 必須保持簡單且固定，避免把 Servlet framework、I/O 或 GC-heavy workload 混入第一階段 microbenchmark。

每次 operation 使用固定大小的 primitive arguments（基本型別參數），以避免 C struct → Java object 物件化成本污染 thread topology 比較。

第二階段另以 docs/JNI_COST_MODEL.md 的 C canonical request → opaque handle → Java facade → DirectByteBuffer 模型測量物件與資料視圖成本。

## 3. 實驗模式

### Attached submission

```text
C event-loop worker
→ AttachCurrentThread()
→ JNI submission/control call
→ Java executor
→ Servlet/application work
→ completion
→ DetachCurrentThread()
```

實際長生命週期版本也必須測量 thread 已經 attached、只重複 JNI submission 的情況；不得用此模式讓 C event-loop thread 執行 Servlet application code。

### Single bridge

```text
C producer/worker
→ bounded queue push
→ bridge thread
→ JNI call
→ completion
→ producer/owner
```

### Bridge pool

```text
C producer/worker
→ bounded queue
→ bridge thread pool
→ JNI call
→ completion
```

第一階段 pool size 至少測 1、2、4；實際上限由 CPU count 與測試環境決定。

## 4. 必測指標

- operation latency
- queue wait latency
- JNI invocation latency
- throughput
- p50／p95／p99 tail latency（尾端延遲）
- CPU utilization
- thread count
- failed/cancelled operations
- allocation／GC activity（可取得時）

每個結果必須記錄 Ckarta commit、JDK、compiler、OS、kernel、CPU、測試參數與 warm-up／measurement 設定。

## 5. 控制變因

第一階段固定：

- Linux x86_64
- OpenJDK 21
- primitive JNI arguments
- 不建立 Java String／Array
- 不進 Servlet container
- 固定 operation count
- 固定 producer count
- 固定 target method

不得用第一階段結果宣稱完整 Ckarta request path 的效能。

## 6. 解讀原則

不能只看平均值。若 bridge queue 降低 JNI thread lifecycle 複雜度，卻提高 p99 latency，必須保留此 trade-off（取捨）。

不能因 attached submission 在 microbenchmark 勝出，就直接決定正式架構；必須再把 connection ownership、AsyncContext、cancellation、shutdown、Servlet executor 與 JNI lifetime 納入整體測試。

反之，bridge model 也不能因 ownership 較簡單就假定效能較佳。

## 7. 與 upstream 的交叉比對

Nginx 1.30.4：worker process 與 master control 的 ownership／lifecycle 可作 C worker ownership 的參考，但 Ckarta 第一階段仍是單一 JVM process。

來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/unix/ngx_process_cycle.c

Tomcat 11.0.25：NioEndpoint 的 network endpoint、poller、acceptor 與 executor 分層可作 Java side thread role 的參考；不能直接複製其 thread count。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java

## 8. 學術背景

Zeldovich 等人的研究說明 event-driven application 可以透過 coarse-grained parallelism（粗粒度平行性）利用多核心，同時避免把所有同步問題都推到 fine-grained synchronization（細粒度同步）。

來源：
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

Welsh、Culler、Brewer 的 SEDA 研究則提供 explicit stage／queue／overload control 的設計背景；Ckarta 不因此宣稱自己是 SEDA。

DOI：
https://doi.org/10.1145/502059.502057

Capriccio 則提供 thread-based server（執行緒型伺服器）可擴展性的另一組經驗，因此本 benchmark 必須同時把 direct thread path 視為正式候選，而不是把 event-driven 視為先驗正解。

DOI：
https://doi.org/10.1145/945445.945471

## 9. OpenJDK 21 約束

Invocation API：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html

JNI functions：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

測試不得跨 thread 傳遞 `JNIEnv*`。需要 JNI 的 native thread 必須使用自己的 attachment／detach 生命週期。

## 10. Current harness boundary

`bench/jni/` 目前是低階 harness，而非正式 Ckarta request benchmark。其 direct 模式測量已 attach native worker 直接呼叫固定 primitive Java method；bridge 模式測量 bounded C queue 加 bridge thread 的 JNI invocation。它沒有 Java Servlet executor、Servlet container、canonical request descriptor、DirectByteBuffer request facade、AsyncContext 或正式 completion routing。

因此本 harness 只能回答低階的 JNI invocation／queue handoff／thread attachment 問題；它不能直接回答 A/B/C 哪種正式 Ckarta topology 最佳。正式 benchmark 必須在相同 canonical request workload 上加入 Java executor、request lifetime、completion routing、cancellation 與必要的 Servlet semantics。

## 10. 現況

本文件只定義實驗；正式 thread topology 在可重現 benchmark 完成前維持暫定。

## 11. 理論修正

Little 定律 `L = λW` 與 SEDA 所強調的 explicit stage／queue／resource control 顯示，新增 JNI handoff 不只增加單次 call cost，也可能新增排隊節點。故正式 benchmark 必須將 queue wait 與 Java executor scheduling 分開量測。

同時，Servlet 6.1 的 AsyncContext 與 non-blocking I/O 意味著 Java request lifecycle 可以超出一次同步 service invocation；因此 benchmark 必須包含 asynchronous completion 與 cancellation，不得只測一個同步 Java method。

完整理論研究見 docs/WEB_SERVER_THEORY_SERVLET_NGINX.md。
