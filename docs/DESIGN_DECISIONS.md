# Ckarta 設計決策矩陣

本文件把 Ckarta 目前最重要的架構選擇與固定版本的 Nginx／Apache Tomcat 原始碼、正式規格及學術研究對照。

## 1. 事件驅動核心

決策：C 資料平面採 worker + event loop；Linux 優先 epoll。

Nginx 證據：

third_party/nginx/src/event/ngx_event.c
- ngx_process_events_and_timers()

third_party/nginx/src/event/modules/ngx_epoll_module.c
- ngx_epoll_process_events()
- epoll_wait()

Tomcat 證據：

third_party/tomcat/java/org/apache/tomcat/util/net/NioEndpoint.java
- Poller.run()
- Poller.processKey()

判定：C 化通過。

理由：兩者都把 readiness／event processing 與上層 protocol processing 分開；Ckarta 將低階網路事件留在 C。

限制：不可因此宣稱 Ckarta 一定比 Tomcat NIO 快。

## 2. HTTP parser

決策：HTTP/1.1 parser（解析器）留在 C，建立單一 framing interpretation（訊息框架解讀）。

Nginx：

src/http/ngx_http_request.c
- ngx_http_process_request_line()
- ngx_http_process_request_headers()
- ngx_http_process_request()

Tomcat：

Http11Processor.service()
- parseRequestLine()
- parseHeaders()
- prepareRequest()

判定：C 化通過，但必須以 RFC 9112 建立規範語意。

理由：Ckarta 的 frontend／proxy／upstream 若重複使用不同 framing interpretation，會形成 request smuggling（請求走私）風險。

規格：
https://www.rfc-editor.org/rfc/rfc9112.html

## 3. C memory pool

決策：請求暫態資料使用 C memory pool；Java heap 不納入 C pool。

Nginx：

src/core/ngx_palloc.c
- ngx_palloc()
- ngx_palloc_small()
- ngx_palloc_block()
- ngx_palloc_large()
- ngx_destroy_pool()
- ngx_pool_cleanup_add()

判定：C 化通過。

理由：pool lifetime 與大量暫態配置高度相容。

限制：Ckarta 必須自己定義 owner/lifetime；不可直接假設 Nginx pool API 語意。

## 4. Timer

第一版決策：timer tree（計時器樹）。

Nginx event loop 會以 timer 狀態決定等待時間並在事件處理後 expire timers。

研究方向：timing wheel。

學術來源：

George Varghese、Anthony Lauck，
"Hashed and Hierarchical Timing Wheels: Efficient Data Structures for Implementing a Timer Facility"
IEEE/ACM Transactions on Networking 5(6), 1997, 824–834。
DOI：
https://doi.org/10.1109/90.650142

該研究分析 timing wheel 在適用範圍可提供攤銷 O(1) 的 timer 操作，但這不是 Ckarta 採用 timing wheel 的充分理由。

決策：沒有 benchmark 前不替換第一版 timer tree。

## 5. Servlet execution

決策：Java executor／thread pool 執行 Servlet application。

Tomcat：

NioEndpoint.SocketProcessor.doRun()
→ handler.process()

Http11Processor.service()
→ CoyoteAdapter.service()

CoyoteAdapter.service()
→ Container Pipeline

StandardWrapperValve.invoke()
→ wrapper.allocate()
→ ApplicationFilterFactory.createFilterChain()
→ filterChain.doFilter()
→ Servlet.service()

判定：Java 保留。

理由：Servlet lifecycle、Filter Chain、AsyncContext、Context、Wrapper 都是 Java container semantics。

學術對照：

Rob von Behren、Jeremy Condit、Feng Zhou、George C. Necula、Eric Brewer，
"Capriccio: Scalable Threads for Internet Services"
SOSP 2003。
DOI：
https://doi.org/10.1145/945445.945471

結論不是「thread model（執行緒模型）一定較快」，而是高並行網路服務可採不同執行模型；Ckarta 將 Servlet application 執行與 C socket event processing 明確分離。

## 6. 階段與過載控制

決策：採用 bounded queue（有界佇列）與 stage isolation（階段隔離）的思想；不直接宣稱是 SEDA。

學術來源：

Matt Welsh、David Culler、Eric Brewer，
"SEDA: an architecture for well-conditioned, scalable Internet services"
ACM SIGOPS Operating Systems Review 35(5), 2001, 230–243。
DOI：
https://doi.org/10.1145/502059.502057

Ckarta 使用其作為過載控制與階段設計的理論依據，不聲稱實作完整 SEDA。

## 7. Worker ownership

決策：先用 worker ownership 與 sharding（分片），再考慮 lock-free。

學術來源：

Nickolai Zeldovich、Alexander Yip、Frank Dabek、Robert T. Morris、David Mazières、Frans Kaashoek，
"Multiprocessor Support for Event-Driven Programs"
USENIX ATC 2003。
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

研究顯示可讓不共享可變狀態的事件平行執行，因此支持 Ckarta 優先減少共享狀態。

## 8. Lock-free data structure

決策：不預設使用。

學術來源：

Maged M. Michael，
"High Performance Dynamic Lock-Free Hash Tables and List-Based Sets"
SPAA 2002。
DOI：
https://doi.org/10.1145/564870.564881

此研究不能推出「lock-free 在 Ckarta 一定較快」。

Ckarta 若未來採 lock-free，必須另外證明：

- contention（競爭）是實際瓶頸；
- memory reclamation（記憶體回收）正確；
- 線性化點明確；
- benchmark 優於 simpler design（較簡單設計）。

## 9. TLS

決策：C termination（C 終止 TLS）作為主要資料平面。

Nginx：
HTTP SSL module（HTTP SSL 模組）

Tomcat：
NioEndpoint / SecureNioChannel / SocketProcessor.doRun()

判定：C 化通過，但實際函式庫尚未選定，不得假造 Ckarta TLS API。

## 10. 靜態檔案

決策：C fast path（快速路徑）。

Tomcat NioEndpoint 已存在 sendfile processing 路徑；Nginx 具有獨立 static/sendfile 路徑。

Ckarta 可在不需要 Servlet semantics、compression（壓縮）或 Java 產生內容時避免進 JVM。

限制：TLS 或內容轉換可能要求資料經 CPU 處理，因此不得宣稱所有靜態檔案均是端到端 zero-copy（零拷貝）。

## 11. Session

決策：Session semantics（會話語意）留 Java。

C 僅處理 transport/routing metadata（傳輸／路由資訊）。

理由：Servlet API 定義 Session 的應用程式語意；不要在 C 與 Java 建立雙重 authority（權威來源）。

## 12. Request lifetime

決策：connection lifetime（連線生命週期）與 Servlet request lifetime（Servlet 請求生命週期）必須可分離。

Tomcat CoyoteAdapter.asyncDispatch() 已證明 async request 在初始 service 返回後仍可能有後續 dispatch。

因此 Ckarta 必須支持：

CONNECTION
→ SERVLET_ACTIVE
→ ASYNC_WAIT
→ COMPLETION／TIMEOUT／ERROR
→ OUTPUT
→ KEEPALIVE/CLOSE

## 13. 目前信心等級

高：

- C event loop
- C HTTP parsing
- C connection state
- Java Servlet semantics
- Java Servlet executor
- C memory pool
- JNI 必須粗粒度化
- connection／async lifetime 分離

中：

- timer tree vs timing wheel
- cache metadata ownership
- compression placement
- cross-worker shared memory

尚未決定：

- TLS library
- allocator strategy beyond pool
- HTTP/2 implementation
- HTTP/3 implementation
- exact JNI ABI
- exact Java package layout
- build system

「尚未決定」不得在程式碼中預先固化成假定 API。
