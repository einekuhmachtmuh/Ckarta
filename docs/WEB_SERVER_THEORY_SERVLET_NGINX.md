# Web server 理論、Jakarta Servlet 6.1 與 Nginx 比對研究

## 1. 研究問題

本研究不以「哪個產品遵守哪個規範」作為主要評判，而問：

在大量並行 HTTP workload 下，若目標是最大化穩定吞吐、控制延遲與尾端延遲、避免資源過度承諾，同時保留 Jakarta Servlet 6.1 應用程式語意，哪一種 C／Java 分層最合理？

Jakarta Servlet 6.1 定義伺服器端 API 與容器契約；它不是完整 Web server 的效能架構規範。6.1 的最小 Java SE 版本為 17，且新增／強化包含 ServletInputStream 與 ServletOutputStream 的 ByteBuffer 支援。來源：https://jakarta.ee/specifications/servlet/6.1/

## 2. 理論上的 Web server 目標

先把請求路徑抽象成若干服務階段：

C 網路／HTTP 階段
→ 跨語言交接
→ Java 容器／Servlet 階段
→ 跨語言回傳
→ C 輸出／網路階段

令第 i 個階段的平均服務時間為 S_i，等待時間為 Q_i，則單一請求的平均端到端處理時間可寫成：

W = Σ_i (S_i + Q_i) + W_external

其中 W_external 表示網路、儲存體或其他不屬於內部服務階段的等待。

對穩態系統，Little 定律為：

L = λW

其中 L 是系統內平均請求數，λ 是平均到達率，W 是平均逗留時間。Little 的原始論文給出的成立條件包含有限平均值與特定穩態條件；因此 Ckarta 只能把它作為系統級不變關係，而不是任意 workload 下的即時估算。

來源：
John D. C. Little, “A Proof for the Queuing Formula: L = λW”, Operations Research 9(3), 1961, 383–387.
DOI：https://doi.org/10.1287/opre.9.3.383

因此：

- 額外增加一個跨層階段，不能只問「一次呼叫多慢」，還必須問它是否形成新的排隊節點。
- 若某階段的服務能力接近到達率，該階段的等待時間會成為端到端延遲的重要部分。
- 因此 centralized bridge（中央橋接層）若把大量請求集中到有限數目的執行緒，可能成為 throughput bottleneck（吞吐瓶頸），即使單次 JNI crossing（JNI 邊界穿越）本身很快。

## 3. Event-driven server 的理論收益

Banga、Mogul、Druschel 對 UNIX event delivery 的研究指出，傳統 select() 在大量 file descriptors（檔案描述元）下具有可擴展性問題，並提出更具可擴展性的事件遞送機制。

來源：
Gaurav Banga, Jeffrey C. Mogul, Peter Druschel, “A Scalable and Explicit Event Delivery Mechanism for UNIX”, USENIX ATC 1999.
https://www.usenix.org/conference/1999-usenix-annual-technical-conference/scalable-and-explicit-event-delivery-mechanism

Nginx 目前仍採 worker process（工作者程序）＋event-based model（事件式模型），並在 Linux 使用 epoll、在其他作業系統使用對應事件機制。官方文件也指出 worker_connections 同時限制 client 與 upstream 連線，因此資源上限本身就是架構的一部分。

來源：
https://nginx.org/en/docs/beginners_guide.html
https://nginx.org/en/docs/events.html
https://nginx.org/en/docs/ngx_core_module.html

Ckarta 應吸收的不是「Nginx 就一定最快」，而是：

- I/O multiplexing（輸入輸出多路複用）把大量 idle connection 的等待成本從 thread 數量中分離。
- connection state（連線狀態）應是顯式狀態機。
- resource limit（資源上限）必須是設計的一部分，而不是事後加的防護。

## 4. SEDA 對 Ckarta 更重要的啟發

Welsh、Culler、Brewer 的 SEDA 將 Internet service（網際網路服務）拆成 event-driven stages（事件驅動階段），由 explicit queues（明確佇列）連接，並把 thread pool sizing（執行緒池大小）、event batching（事件批次化）與 load shedding（負載丟棄）納入動態資源控制。

來源：
Matt Welsh, David Culler, Eric Brewer, “SEDA: An Architecture for Well-Conditioned, Scalable Internet Services”, SOSP 2001, pp. 230–243.
期刊版 DOI：https://doi.org/10.1145/502059.502057
會議版 DOI：https://doi.org/10.1145/502034.502057

對 Ckarta 的重要結論不是「把整個系統改成 SEDA」，而是：

跨 C／Java 邊界若形成 stage，就必須明確定義：

queue capacity
→ admission control
→ service capacity
→ overload behavior
→ cancellation
→ ownership

因此 JNI bridge 不應只是「幫忙呼叫 Java 的包裝層」，而應被視為一個可能形成 queueing point（排隊節點）的系統元件。

## 5. Servlet 6.1 與高效能 server 並不矛盾

Servlet 6.1 提供 AsyncContext，讓請求可以進入 asynchronous execution context（非同步執行上下文）；官方 API 明確定義 AsyncContext 代表由 ServletRequest 啟動的非同步操作，而且 timeout、error、complete、dispatch 都具有容器語意。

來源：
https://jakarta.ee/specifications/servlet/6.1/apidocs/jakarta.servlet/jakarta/servlet/asynccontext

Servlet 規格的 async model（非同步模型）本身就是為了避免一個慢資源永久佔據原始 request thread。歷史規格文字也明確描述 blocking operation（阻塞操作）會消耗有限 thread resource（執行緒資源），並由 asynchronous processing（非同步處理）把等待與 request thread 分離。

來源：
https://jakarta.ee/specifications/servlet/6.0/jakarta-servlet-spec-6.0

因此對 Servlet developer（Servlet 開發者）友善的方向不是把 Servlet API 縮成 C-style callback；而是：

C 盡可能把 I/O 與 connection scheduling 做好，
Java 仍提供完整 Servlet semantics（Servlet 語意），
AsyncContext 與 non-blocking I/O（非阻塞輸入輸出）作為兩側之間的重要協調機制。

6.1 另外新增 ServletInputStream／ServletOutputStream 的 ByteBuffer 支援，這讓未來 Ckarta 可以研究更少的資料轉換，而不必把 HTTP stack 本身搬進 Java。

## 6. Nginx、Tomcat、Servlet 6.1 的差異

### Nginx

Nginx 以 C 為主、event-driven、worker process、connection state machine 與 bounded resources（有界資源）為核心。這非常適合大量網路等待。

### Tomcat

Tomcat NIO endpoint 將 Poller、Acceptor 與 socket processing 分層。固定 11.0.25 原始碼中的 Poller.run() 使用 Selector，並將 ready socket event 交給 processKey()；SocketProcessor.doRun() 再進入 handler。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java

### Jakarta Servlet

Servlet 6.1 的重點是 application-visible semantics（應用程式可見語意）：request／response、Filter、Session、dispatch、async、listener 與生命週期。

來源：
https://jakarta.ee/specifications/servlet/6.1/

三者其實位於不同 abstraction level（抽象層級），因此不應直接問「Servlet 是否應該像 Nginx」。

正確問題是：

「哪一部分的責任最適合留在 C，哪一部分必須留在 Java，兩者在哪裡形成最少且最有價值的 handoff？」

## 7. Ckarta 整體方向是否需要改變

專業評定：**需要局部修正，但不需要推翻整體方向。**

原方向：

Nginx 式 C data plane
+
Tomcat 式 Java Servlet container
+
JNI boundary

仍然合理。

需要修正的是「JNI bridge」在架構上的角色。

不再把：

C worker
→ central JNI bridge
→ Java

視為預設 topology。

改成：

C event-driven data plane
→ bounded semantic handoff
→ Java container／executor
→ Servlet application

其中 semantic handoff（語意交接）可以由：

A. attached worker submission（已附加工作者提交）；
B. worker-group bridge（工作者群組橋接）；
C. central bridge pool（中央橋接池）

之一實現。

但三者只能由同 workload benchmark 決定。

## 8. 更重要的新架構原則

Ckarta 的最佳化目標應從：

「盡可能把東西搬到 C」

改成：

「把高 I/O、低語意密度、可由狀態機表達的工作留在 C；把高 Servlet semantic density（Servlet 語意密度）工作留在 Java；使跨界次數與跨界資料量受控。」

可把一次 request 的跨界成本抽象為：

C_cross = N_cross * C_call
        + B_cross * C_byte
        + Q_cross

其中：

N_cross = 跨界次數
B_cross = 跨界資料量
C_call = 每次跨界固定成本
C_byte = 每 byte 的資料處理／轉換成本
Q_cross = 因跨界形成的 queueing overhead（排隊額外成本）

這個模型不是 HotSpot 的精確成本公式，而是 architecture decision model（架構決策模型）。

因此：

- 減少 N_cross：避免每個 header、body chunk、write operation 都跨 JNI。
- 減少 B_cross：避免 C struct → 大量 Java object。
- 控制 Q_cross：所有 bridge queue 必須有界並可觀測。
- 不以「zero-copy」作為唯一目標，因為 TLS、壓縮、Servlet API semantics 或資料格式轉換仍可能要求 CPU processing。

## 9. 「理想 Web server」與 Servlet developer 友善性的交集

理論上理想的高併發 server 應同時滿足：

1. 大量 idle connections 不需要等比例增加執行緒。
2. CPU-intensive／blocking work 不得無界污染 I/O reactor。
3. 每個有限資源都有容量上限。
4. 過載時服務應 graceful degradation（漸進式退化），而不是無界排隊。
5. cancellation、timeout、connection close 與 resource lifetime 必須一致。
6. throughput 飽和後，不能透過無限增加 queue 將 latency 推向失控。

但 Servlet developer 不應被迫理解上述機制才能使用 Ckarta。

因此應讓 Java side 保持熟悉的：

HttpServletRequest
HttpServletResponse
Filter
Session
AsyncContext
ServletInputStream
ServletOutputStream

而把：

event loop
connection state
TLS
HTTP framing
backpressure
resource limits

藏在 container implementation（容器實作）中。

這不是「增加抽象層」，而是 Servlet API 本身要求的語意隔離；真正需要避免的是在 C/JNI 內部再疊加沒有責任的轉發 wrapper。

## 10. 最終方向

Ckarta 不改成：

「純 Nginx」

也不改成：

「C 版 Tomcat」

而改成更精確的：

C event-driven network data plane
+
bounded semantic handoff
+
Java Servlet semantic plane
+
shared lifecycle／ownership contract

最需要優先驗證的不是 C 是否比 Java 快，而是：

在相同 HTTP workload 下，哪一個 semantic handoff topology 可以在不破壞 Servlet semantics 的前提下，同時最小化：

JNI crossings
crossed bytes
queue wait
thread scheduling
memory retention
tail latency

這會成為下一階段 benchmark 與正式 JNI ABI 設計的理論基線。

## 11. 研究來源

Jakarta Servlet 6.1：
https://jakarta.ee/specifications/servlet/6.1/
https://jakarta.ee/specifications/servlet/6.1/apidocs/jakarta.servlet/jakarta/servlet/asynccontext

Nginx：
https://nginx.org/en/docs/beginners_guide.html
https://nginx.org/en/docs/events.html
https://nginx.org/en/docs/ngx_core_module.html

Tomcat 11.0.25：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java

Banga, Mogul, Druschel：
https://www.usenix.org/conference/1999-usenix-annual-technical-conference/scalable-and-explicit-event-delivery-mechanism

Welsh, Culler, Brewer：
https://doi.org/10.1145/502059.502057
https://doi.org/10.1145/502034.502057

Little：
https://doi.org/10.1287/opre.9.3.383
