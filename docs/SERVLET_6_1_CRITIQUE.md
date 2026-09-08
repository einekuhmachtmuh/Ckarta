# Jakarta Servlet 6.1 中立技術批判與 Ckarta 相容性策略

## 1. 結論先行

專業評定：Ckarta 不應放棄 Servlet 6.1，也不應把 Servlet 6.1 的內部物件模型與執行緒模型當作整個 Web server 的內部架構。

建議方向：

- 對外：大程度、實質性遵守 Servlet 6.1 的 API 與 container semantics（容器語意），以 TCK 為硬性驗證門檻。
- 對內：不強制採 Servlet 規格的 request object（請求物件）、blocking I/O（阻塞輸入輸出）、thread-per-request（每請求執行緒）等歷史／相容性模型。
- Web server data plane（資料平面）：保留 C event-driven、non-blocking I/O、顯式 connection state、bounded resources（有界資源）。
- Servlet plane（Servlet 平面）：保留 Java container、Filter、Listener、Session、dispatch、AsyncContext 與 application lifecycle。
- 兩者間：採最薄且語意完整的 bounded semantic handoff（有界語意交接）。

因此「Servlet container」仍是 Ckarta 的核心身份，但不再等於「所有 HTTP request 都必須先物件化為完整 Java Servlet request」。

## 2. Servlet 6.1 的核心優點

### 2.1 穩定的 application contract（應用程式契約）

Servlet 以 Request、Response、Filter、Session、dispatch、listener 等抽象提供成熟且廣泛使用的 application contract。

這對 Ckarta 的價值不是效能，而是 application compatibility（應用程式相容性）。

### 2.2 AsyncContext 已經承認同步 thread model 的瓶頸

規格直接說明，在 Servlet 中等待 JDBC、遠端 Web service、JMS 或其他事件會讓 thread 被 blocking，可能造成 thread starvation（執行緒耗盡）與整個容器的服務品質下降。

AsyncContext 允許 request thread 返回 container，再於稍後由另一個 thread／callback 完成或 dispatch。

來源：
https://jakarta.ee/specifications/servlet/6.1/jakarta-servlet-spec-6.1.html

因此 Servlet 6.1 本身已經部分承認：

application execution
不應等同於
resource waiting。

### 2.3 6.1 的 non-blocking I/O

Servlet 6.1 提供 ReadListener、WriteListener、isReady、isFinished 與 ByteBuffer 相關 API。

這使 Servlet application 可以使用比較接近 event-driven I/O 的模型。

來源：
https://jakarta.ee/specifications/servlet/6.1/jakarta-servlet-spec-6.1.html

這是對 Ckarta 非常有價值的相容性表面。

## 3. Servlet 6.1 的主要架構限制

### 3.1 同步 request/response API 仍以可變物件為中心

ServletRequest、ServletResponse、wrapper、attributes、headers、session 等都是高度狀態性的 Java object。

這對應用程式非常方便，但對 native event-driven data plane 不自然。

若 Ckarta 每次 request 都建立大量 Java mirror object：

C request
→ hundreds of Java fields／objects

則會把 Web server 的 native fast path 重新變成 object construction workload。

因此 Ckarta 應採 lazy materialization（延遲物件化）：

只有當 Servlet application 真的要求某個語意時才建立／轉換相應 Java view。

### 3.2 Thread safety 責任部分轉移給 application

規格明確指出 request／response implementation 除特定 async 操作外，不保證 thread safe；application 必須在多 thread 使用時自行處理同步。

這是合理的 application-level trade-off，但它表示 container 內部必須非常準確地管理：

request lifetime
response lifetime
dispatch boundary
wrapper identity
concurrency boundary。

來源：
https://jakarta.ee/specifications/servlet/6.1/jakarta-servlet-spec-6.1.html

### 3.3 AsyncContext 是強大的相容性機制，但增加狀態空間

一個 async cycle 可能包含：

startAsync
→ application return
→ asynchronous event
→ dispatch／complete／timeout／error
→ new async cycle

此外規格還要求：

- listener notification timing
- wrapper preservation
- dispatch ordering
- original request/response identity
- response commit timing
- timeout behavior

這些都是必要語意，但會增加 container state machine complexity（狀態機複雜度）。

因此 Ckarta 不應把 AsyncContext 當成一個簡單 boolean async。

### 3.4 Non-blocking I/O 的使用條件仍有額外狀態

Servlet non-blocking I/O 需要 async processing。

例如 setReadListener、setWriteListener、isReady、callback ordering 等形成一套 callback/state machine。

這比直接給 native HTTP layer 一個 readiness event 更複雜。

但這不是理由去移除 API；它是 Java compatibility surface。

Ckarta 的策略應該是：

C event
→ Java ReadListener／WriteListener semantic event

而不是：

Java callback
→ 自己重新管理 socket readiness。

## 4. Servlet specification 不應支配 Web server 的所有設計

這是本研究最重要的批判。

Servlet 6.1 解決的是：

「Java Web application 如何與 container 互動」

而不是：

「如何以最小 CPU／memory cost 處理十萬個 idle connections」。

後者屬於 operating system、network server、concurrency architecture 與 queueing system 的問題。

因此：

Servlet 6.1
≠
Web server architecture specification。

這也是為什麼 Nginx、Tomcat 與其他高效能 server 可以採用完全不同的 network architecture，而仍提供不同形式的 application execution。

## 5. Nginx 的反例

固定 Nginx 1.30.4 的：

src/event/ngx_event.c

ngx_process_events_and_timers() 直接圍繞：

- timer
- accept handling
- event processing
- posted events

形成 event-loop core。

FastCGI 模組則使用 upstream abstraction，處理：

- connection
- timeout
- buffering
- request body
- response processing
- retry／failure policy

來源：

https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/ngx_event.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/modules/ngx_http_fastcgi_module.c
https://nginx.org/en/docs/http/ngx_http_fastcgi_module.html

這說明：

network scheduling
與
application execution

可以被非常明確地分離。

## 6. Tomcat 的折衷

固定 Tomcat 11.0.25 的 NioEndpoint：

Poller
→ processKey
→ SocketProcessor
→ Handler

這是一個 Java NIO event／processor／executor 混合模型。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java

而 Servlet application 最終仍進入：

CoyoteAdapter
→ Container Pipeline
→ Filter Chain
→ Servlet

因此 Tomcat 已經展示一個關鍵事實：

Servlet API 與底層 socket event model 不需要是同一個 abstraction。

Ckarta 可以比 Tomcat 更進一步把 network data plane 下沉至 C，而仍保持 Servlet semantics。

## 7. 學術研究的中立比較

### Flash

Pai、Druschel、Zwaenepoel 的 Flash 研究比較 multi-process、multi-thread、single-process event-driven 與 asymmetric multi-process event-driven。

重要結論之一是：沒有一種 concurrency architecture 在所有 workload 都最佳；cached workload 與 disk-bound workload 的最佳策略可以不同。

來源：
Vivek S. Pai, Peter Druschel, Willy Zwaenepoel,
“Flash: An Efficient and Portable Web Server”,
USENIX ATC 1999, pp. 199–212.
https://www.usenix.org/conference/1999-usenix-annual-technical-conference/flash-efficient-and-portable-web-server

這反對「Servlet thread model 必定不好」與「event-driven 必定最好」兩種極端結論。

### JAWS

Hu、Pyarali、Schmidt 的 JAWS 研究將 concurrency strategy、event dispatching、I/O strategy、protocol pipeline 等獨立出來，並主張 Web server 可以依 hardware、OS、traffic 與 workload 自適應。

來源：
https://www.dre.vanderbilt.edu/JAWS/papers/webframeworks.pdf

這支持 Ckarta 把 Servlet execution strategy 與 C network strategy 解耦。

### SEDA

Welsh、Culler、Brewer 的 SEDA：

stage
→ queue
→ resource control
→ overload control

來源：
https://doi.org/10.1145/502059.502057

對 Ckarta 的價值是提醒：

跨 C／Java handoff 本身就是一個可能產生 queueing overhead 的 stage。

### Little

Little 的 L = λW 提供系統級穩態關係。

來源：
John D. C. Little, “A Proof for the Queuing Formula: L = λW”,
Operations Research 9(3), 1961, 383–387.
https://doi.org/10.1287/opre.9.3.383

如果新增 handoff stage 增加 W，在穩態下也會增加系統內平均 work-in-progress L。

## 8. 對 Servlet 6.1 的逐項判斷

| Servlet 特性 | 理論評價 | Ckarta |
|---|---|---|
| Request／Response object | application 友善，但 object-heavy | 保留語意，lazy materialization |
| Filter Chain | 強大、成熟，但增加 call-chain | 保留 |
| Session | 成熟 application semantics | 保留 Java |
| Listener | lifecycle 控制力強，但增加事件狀態 | 保留 |
| RequestDispatcher | 功能完整，但增加 routing／dispatch state | 保留 |
| AsyncContext | 解決 blocking request thread 問題，但增加 state space | 保留並映射 native lifecycle |
| non-blocking I/O | 有效，但 API 使用狀態較複雜 | 保留並由 C readiness 驅動 |
| blocking InputStream | 相容性高，但可能佔用 executor thread | 保留 |
| ByteBuffer 支援 | 適合 C/native integration | 優先 |
| application thread safety responsibility | API 簡潔，但增加 container／application 邊界責任 | 完整遵守 |
| class loading／deployment | application portability 核心 | 完整遵守 |

## 9. 是否應大程度改變 Ckarta 大方向

答案：不應。

但是「Servlet container」的定義需要重新定位。

錯誤：

Ckarta = Servlet 6.1 execution engine + C network wrapper

較佳：

Ckarta = high-performance C Web server data plane + Servlet 6.1-compatible Java application container

這代表：

### C fast path

HTTP request
→ security
→ route
→ static／proxy／TLS
→ 不必進 JVM

### Servlet path

HTTP request
→ C parse
→ canonical request
→ bounded semantic handoff
→ Java facade
→ Container
→ Filter Chain
→ Servlet

### Async Servlet path

C connection state
↔ Java AsyncContext state

而不是讓 AsyncContext 成為另一個獨立 socket implementation。

## 10. Servlet compatibility strategy

應將相容性分成：

### Level 1：API compatibility

Servlet interfaces、classes、signatures 正確。

### Level 2：semantic compatibility

dispatch、filter ordering、async、listener、session、lifecycle、exception、wrapper identity 等正確。

### Level 3：timing／concurrency compatibility

在規格要求之處維持 callback、dispatch、completion 與 thread-safety semantics。

### Level 4：implementation freedom

在不違反 Level 1–3 的前提下，允許：

- C event loop
- native buffers
- lazy Java object materialization
- custom scheduler
- custom executor topology
- request fast paths
- native static/proxy path

這是 Ckarta 最重要的策略。

## 11. 最終建議

Ckarta 不應從「Servlet container」退回「普通 Web server」。

因為 Servlet 6.1 的 compatibility surface 是專案區別於 Nginx 的主要價值之一。

但 Ckarta 也不應讓 Servlet object model 成為整個 Web server 的 internal representation。

最合理的是：

Servlet 6.1 是 Java application compatibility contract；不是 Ckarta 的全系統 execution architecture。

因此整體方向維持，只把 architecture invariant 精確改成：

C event-driven data plane
+
bounded semantic handoff
+
Servlet 6.1 semantic plane

完整研究與來源：
https://jakarta.ee/specifications/servlet/6.1/
https://jakarta.ee/specifications/servlet/6.1/apidocs/jakarta.servlet/jakarta/servlet/asynccontext
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/ngx_event.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/modules/ngx_http_fastcgi_module.c
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java
https://www.usenix.org/conference/1999-usenix-annual-technical-conference/flash-efficient-and-portable-web-server
https://www.dre.vanderbilt.edu/JAWS/papers/webframeworks.pdf
https://doi.org/10.1145/502059.502057
https://doi.org/10.1287/opre.9.3.383
