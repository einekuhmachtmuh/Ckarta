# Ckarta Nginx／Tomcat Hot Path 基線

本文件現在以固定版本為唯一研究基線。逐函式細節見 docs/FUNCTION_TRACE.md；CGI／FastCGI／Servlet／JNI native boundary（原生邊界）研究見 docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md。

## 1. 固定參考版本

Nginx 1.30.4 stable
commit 017cf98dcce217946572a896f0992370475e189f

Apache Tomcat 11.0.25
commit cbe6e15ee81e2fc6232954292a80cca5d1e84009

OpenJDK 21 Update
`jdk-21.0.8-ga`

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

Nginx 動態 gateway 的重要分支是 FastCGI upstream：Nginx 建立／傳送 FastCGI parameters、request body 與相關 request state，真正的動態 application execution 位於 FastCGI server；這不是在 Nginx worker 中直接把 generic CGI program 當 handler 執行。

來源：

https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/modules/ngx_http_fastcgi_module.c
https://nginx.org/en/docs/http/ngx_http_fastcgi_module.html

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

Tomcat `CGIServlet` 是不同 hot path：Java Servlet 呼叫 `Runtime.exec()`，再進入 JDK `ProcessImpl` native process creation，形成 OS-process／stdio boundary，而不是一般 Servlet 的 JVM-in-process call graph。

來源：

https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/servlets/CGIServlet.java
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/java.base/unix/classes/java/lang/ProcessImpl.java

## 4. 交叉結論

Nginx 與 Tomcat 都具有 I/O readiness／event processing，但上層執行模型不同。

Nginx：

event-driven data plane（事件驅動資料平面）
→ C handler／phase pipeline
→ optional upstream protocol

Tomcat：

Java NIO
→ SocketProcessor
→ protocol processor
→ CoyoteAdapter
→ Container Pipeline
→ Servlet

Ckarta：

C event-driven network data plane
+
bounded semantic handoff
+
Java Servlet executor／container
+
JNI in-process boundary

## 5. Native boundary 的位置很重要

Classic CGI：

request
→ OS process
→ stdio

FastCGI：

request
→ Nginx upstream protocol／socket
→ persistent application process

Tomcat Servlet：

request
→ Java call graph／executor
→ Servlet

Ckarta JNI：

request
→ JNI entry
→ HotSpot runtime／Java call machinery
→ Servlet

因此不能只比較「有沒有 process creation」。Ckarta 的主要問題是 JNI entry、HotSpot call machinery、object／buffer conversion、thread handoff 與 Java scheduling 疊加後的成本。

## 6. Ckarta hot path

Servlet：

C event
→ HTTP framing validation
→ route
→ canonical request descriptor
→ semantic handoff
→ Java executor
→ Java request facade
→ Container
→ Filter Chain
→ Servlet
→ completion
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

## 7. 不變條件

C event loop 不得執行 Servlet application code。

JNI 不得成為每個 header／byte 的細粒度跨邊界。

HTTP framing 必須只有一個權威語意。

C memory ownership 必須可追蹤。

Servlet AsyncContext 可在 Java 方法返回後繼續存在；目前 `CkartaServletAsyncContext` 只完成 API binding prototype，尚未完成 request/container startAsync integration。

Servlet request lifetime 與 TCP connection lifetime 不假設一對一。

## 8. 必須保留的 Nginx 特性

值得吸收：

- event backend abstraction（事件後端抽象）
- connection-oriented handler state
- request-scoped pool
- explicit phase processing
- static file fast path
- upstream state machine
- upstream timeout／buffering／failure policy

不可直接複製：

Nginx 私有 data structure（資料結構）
Nginx module ABI
Nginx configuration semantics（設定語意）

## 9. 必須保留的 Tomcat 特性

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

## 10. 學術依據

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
DOI：https://doi.org/10.1109/90.650142

Lock-free Hash Tables：
Maged M. Michael，
High Performance Dynamic Lock-Free Hash Tables and List-Based Sets。
DOI：https://doi.org/10.1145/564870.564881

CGI／FastCGI／Servlet 比較：
Bhupesh Kothari、Mark Claypool，
Performance Analysis of Dynamic Web Page Generation Technologies，International Network Conference, 2000。
來源：https://web.cs.wpi.edu/~claypool/papers/cgi-perf/

Varsha Apte、Tony Hansen、Paul Reeser，
Performance comparison of dynamic web platforms，Computer Communications 26(8), 888–898 (2003)。
DOI：https://doi.org/10.1016/S0140-3664(02)00221-9

學術來源只用來支持模型、方法與限制，不用來宣稱 Ckarta 已經具有同等效能；歷史平台數字不得直接套用 OpenJDK 21。

## 11. 進一步研究要求

在 real C network module 之前仍需完成：

- connection state transition table
- allocation／free map
- blocking point map
- lock／atomic operation map
- JNI ownership model
- AsyncContext cancellation model
- request framing test corpus
- TCK integration plan
- reproducible benchmark harness
- direct attach／bridge topology benchmark
- event backend 與 connection registry 的 lifetime/cookie integration

其中 Linux `epoll` event backend contract 已有 executable slice，但 listener、accepted connection、nonblocking I/O 與 HTTP request path 尚未接入。

以上每一項都必須以固定 upstream commit、OpenJDK baseline 與 Ckarta commit 為版本基準。

## 12. 理論架構更新

Ckarta 採「C event-driven network data plane + bounded semantic handoff + Java Servlet semantic plane」。這保留 Nginx 的 I/O scalability（輸入輸出可擴展性）與 Tomcat／Servlet 的 application semantics，同時避免把 C event-loop thread 誤用成 Servlet execution thread。

理論依據與數學模型見 docs/WEB_SERVER_THEORY_SERVLET_NGINX.md。

## 13. 2026-09-09 native async bridge slice

新增研究／實作 path：

C connection registry
→ opaque generation handle
→ JNI async bridge
→ CkartaAsyncCycleBinding
→ CkartaAsyncContext terminal gate
→ native terminal arbitration
→ Java local async outcome

固定 Tomcat 11.0.25 source 顯示 `AsyncContextImpl` 將 application-facing `check()` 與 container-internal timeout/error/dispatch/recycle 分開；`Request.startAsync()` 建立／重用 async context 並以 per-cycle state 重新初始化。固定 Nginx event model 則把 connection/event/timer/posted-event 分層。Ckarta 因此採 native registry 作 ownership lookup、Java core 作 Servlet semantics，而沒有把 Tomcat private classes 或 Nginx data structures 直接變成 ABI。

此 slice 尚未進入 HTTP socket hot path，不能用它推導端到端效能。

## 14. 2026-09-09 Linux event backend slice

新增實作 path：

C worker
→ `ck_event_loop`
→ Linux `epoll_wait()`
→ opaque `uint64_t` cookie
→ connection registry validation（下一閘門）
→ connection state machine

`c/event/ck_event_loop.[ch]` 目前是 Linux/POSIX baseline。API 以 `epoll_create1(EPOLL_CLOEXEC)`、`epoll_ctl(ADD/MOD/DEL)` 與 `epoll_wait()` 實作，使用 level-triggered readiness，刻意未提前引入 `EPOLLET`。

event backend 不儲存可被 registry retire 的 raw connection pointer，只傳遞 opaque cookie。真正的 stale-event 防護必須在 notification consumer 重新以 generation-protected registry handle 驗證。

`tests/event/ck_event_loop_test.c` 已覆蓋 registration、readiness、cookie 更新、remove 與 peer close notification；GitHub Actions build-smoke 已在 Ubuntu 24.04／GCC 13.3／Temurin OpenJDK 21.0.12 通過整條 `make test`。
