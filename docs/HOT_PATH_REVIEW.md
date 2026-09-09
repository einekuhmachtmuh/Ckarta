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

Nginx response output 的 `ngx_http_write_filter()` 會保留尚未送出的 output chain；connection send path 未完全完成時回傳 `NGX_AGAIN`，把 pending output 留待後續 writable processing。因此 response generation／output drain 不是同一個 completion boundary。

固定來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_write_filter_module.c

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

Tomcat response output 的 `Http11OutputBuffer` 分離 header composition、active output filters、socket output、`commit()`、`end()` 與 `nextRequest()`／`recycle()`；`Response.isReady()`／`checkRegisterForWrite()` 另管理 non-blocking writable interest 與 callback dispatch。

固定來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/coyote/http11/Http11OutputBuffer.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/coyote/Response.java

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
→ response transaction
→ response serialization
→ bounded output writer
→ writable event continuation
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

目前可執行 HTTP input sub-path：

C epoll readiness
→ registry handle/generation validation
→ short-lived reader pin
→ connection-owned non-blocking recv
→ bounded reader read/process batch
→ HTTP framing
→ body sink
→ request completion／pipelined leftover
→ reader pin release

reader pin 只保護 connection entry 與 heap-backed reader 的 lifetime；registry mutex 不包住 `recv()` 或 HTTP framing。單次 reader dispatch 現受 32 KiB socket read budget 與 32 KiB input processing budget 約束；budget 是 fairness／overload-control policy，不是 HTTP framing limit。

目前可執行 HTTP output sub-path：

response transaction
→ bounded output staging
→ non-blocking `send()`
→ partial-write continuation
→ `CK_EVENT_WRITE`／EPOLLOUT readiness
→ output drain

一次 writable dispatch 最多處理 32 KiB；仍有 pending bytes 時不把 response 視為 socket drained。此為 Ckarta native output integration slice，不代表完整 Servlet response path 已完成。

## 7. 不變條件

C event loop 不得執行 Servlet application code。

JNI 不得成為每個 header／byte 的細粒度跨邊界。

HTTP framing 必須只有一個權威語意。

C memory ownership 必須可追蹤。

Servlet AsyncContext 可在 Java 方法返回後繼續存在；目前 `CkartaServletAsyncContext` 只完成 API binding prototype，尚未完成 request/container startAsync integration。

Servlet request lifetime 與 TCP connection lifetime 不假設一對一。

reader pin 不等於一般 shared ownership；同一 connection reader state 仍以固定 owner worker 推進為原則。

response transaction `FINISHED` 不等於 socket output drained；connection keep-alive recycle 必須等 output／error state 明確收斂。

## 8. 必須保留的 Nginx 特性

值得吸收：

- event backend abstraction（事件後端抽象）
- connection-oriented handler state
- request-scoped pool
- explicit phase processing
- static file fast path
- upstream state machine
- upstream timeout／buffering／failure policy
- pending output chain 與 writable continuation 的分離

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
- response commit／end／nextRequest lifecycle separation
- writable readiness registration 與 application callback separation

reader lifecycle 的參考只限於 Tomcat input buffer 的明確 request boundary／recycle 觀念；不複製其 Java object model。
