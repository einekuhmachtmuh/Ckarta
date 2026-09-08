# Ckarta Nginx／Tomcat Hot Path 基線

本文件現在以固定版本為唯一研究基線。逐函式細節見 docs/FUNCTION_TRACE.md。

## 1. 固定參考版本

Nginx 1.30.4 stable
commit 017cf98dcce217946572a896f0992370475e189f

Apache Tomcat 11.0.25
commit cbe6e15ee81e2fc6232954292a80cca5d1e84009

參考來源與版本治理見 docs/REFERENCE_SOURCES.md。

## 2. Nginx

已核對的主要 hot path：

ngx_process_events_and_timers()
→ ngx_process_events()
→ ngx_epoll_process_events()（Linux）
→ ngx_http_init_connection()
→ ngx_http_wait_request_handler()
→ ngx_http_process_request_line()
→ ngx_http_process_request_headers()
→ ngx_http_process_request()
→ ngx_http_handler()
→ ngx_http_core_run_phases()

主要來源：

third_party/nginx/src/event/ngx_event.c
third_party/nginx/src/event/modules/ngx_epoll_module.c
third_party/nginx/src/http/ngx_http_request.c
third_party/nginx/src/http/ngx_http_core_module.c
third_party/nginx/src/core/ngx_palloc.c

## 3. Tomcat

已核對的主要 hot path：

NioEndpoint.Poller.run()
→ Poller.processKey()
→ SocketProcessor.doRun()
→ Http11Processor.service()
→ CoyoteAdapter.service()
→ Container Pipeline
→ StandardWrapperValve.invoke()
→ ApplicationFilterFactory.createFilterChain()
→ ApplicationFilterChain.doFilter()
→ Servlet.service()

主要來源：

third_party/tomcat/java/org/apache/tomcat/util/net/NioEndpoint.java
third_party/tomcat/java/org/apache/coyote/http11/Http11Processor.java
third_party/tomcat/java/org/apache/catalina/connector/CoyoteAdapter.java
third_party/tomcat/java/org/apache/catalina/core/StandardWrapperValve.java

## 4. 交叉結論

Nginx 與 Tomcat 都具有 I/O readiness／event processing，但它們的上層執行模型不同。

Nginx：

event-driven data plane（事件驅動資料平面）
→ C handler
→ phase pipeline

Tomcat：

Java NIO
→ SocketProcessor
→ protocol processor
→ CoyoteAdapter
→ Container Pipeline
→ Servlet

因此 Ckarta 採：

C event-driven network data plane
+
Java executor-based Servlet execution（Java 執行器式 Servlet 執行）

## 5. 必須保留的 Nginx 特性

值得吸收：

- event backend abstraction（事件後端抽象）
- connection-oriented handler state
- request-scoped pool
- explicit phase processing
- static file fast path
- upstream state machine

不可直接複製：

Nginx 私有 data structure（資料結構）
Nginx module ABI
Nginx configuration semantics（設定語意）

## 6. 必須保留的 Tomcat 特性

值得吸收：

- Connector／protocol separation
- Container hierarchy
- Pipeline／Valve
- Filter Chain
- Servlet lifecycle
- AsyncContext semantics（AsyncContext 語意）
- executor／processor separation
- Coyote／Catalina boundary

不可直接複製：

Tomcat internal class hierarchy
Tomcat internal lifecycle contracts
Tomcat private connector implementation

## 7. Ckarta hot path

Servlet：

C event
→ HTTP framing validation
→ route
→ JNI request descriptor
→ Java request facade
→ Container
→ Filter Chain
→ Servlet
→ JNI response descriptor
→ C output pipeline
→ TLS
→ socket

Static：

C event
→ HTTP parser
→ security
→ route
→ file
→ sendfile／buffered output
→ TLS
→ socket

Proxy：

C event
→ HTTP parser
→ security
→ route
→ upstream selection
→ upstream I/O
→ output filter
→ TLS
→ socket

## 8. 不變條件

C event loop 不得執行 Servlet application code。

JNI 不得成為每個 header／byte 的細粒度跨邊界。

HTTP framing 必須只有一個權威語意。

C memory ownership 必須可追蹤。

Servlet AsyncContext 可在 Java 方法返回後繼續存在。

Servlet request lifetime 與 TCP connection lifetime 不假設一對一。

## 9. 學術依據

SEDA：

Matt Welsh、David Culler、Eric Brewer，
SEDA: An Architecture for Well-Conditioned, Scalable Internet Services，
ACM SIGOPS Operating Systems Review 35(5), 2001, 230–243。
DOI：https://doi.org/10.1145/502059.502057

Capriccio：

Rob von Behren、Jeremy Condit、Feng Zhou、George C. Necula、Eric Brewer，
Capriccio: Scalable Threads for Internet Services，
SOSP 2003。
DOI：https://doi.org/10.1145/945445.945471

Timing Wheels：

George Varghese、Tony Lauck，
Hashed and Hierarchical Timing Wheels: Data Structures for the Efficient Implementation of a Timer Facility。
DOI：https://doi.org/10.1145/41457.37504

Lock-free Hash Tables：

Maged M. Michael，
High Performance Dynamic Lock-Free Hash Tables and List-Based Sets。
DOI：https://doi.org/10.1145/564870.564881

學術來源只用來支持模型與限制，不用來宣稱 Ckarta 已經具有同等效能。

## 10. 進一步研究要求

正式實作前仍需完成：

- connection state transition table
- allocation／free map
- blocking point map
- lock／atomic operation map
- JNI ownership model
- AsyncContext cancellation model
- request framing test corpus
- TCK integration plan
- reproducible benchmark harness

以上每一項都必須以固定 upstream commit 與 Ckarta commit 為版本基準。
