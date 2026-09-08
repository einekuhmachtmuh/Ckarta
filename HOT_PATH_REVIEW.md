# Ckarta Nginx／Tomcat Hot Path 與 Whole Path 基線分析

## 1. 目的

本文件不是說 Ckarta 已經重現 Nginx 或 Tomcat。

目的在於回答：

「哪些具體執行路徑值得移到 C、哪些必須保留 Java，以及哪些 Nginx/Tomcat 機制不能直接拼接？」

## 2. Nginx 事件路徑

已核對官方 development guide 與 source：

Nginx event loop（事件迴圈）由：

ngx_process_events_and_timers()

反覆驅動。

在 Linux，正常事件處理會走 epoll notification（epoll 事件通知），官方開發文件明確指出會呼叫 epoll_wait()。

證據：

https://nginx.org/en/docs/dev/development_guide.html
https://github.com/nginx/nginx/blob/master/src/event/ngx_event.c

官方文件描述的基本次序包含：

1. 找到最近到期 timer（計時器）。
2. 等待 I/O event（輸入輸出事件）。
3. 執行 event handler（事件處理器）。
4. 處理 posted events（已排程事件）。
5. 到期 timer。
6. 再處理 posted events。

Ckarta 應保留這個思想，但不直接複製 Nginx 原始碼。

## 3. Nginx 記憶體路徑

已核對：

src/core/ngx_palloc.c

關鍵函式：

ngx_create_pool
ngx_palloc
ngx_pcalloc
ngx_destroy_pool
ngx_pool_cleanup_add

Nginx 官方文件指出，大多數 allocation（配置）由 pool 管理，pool destroy 時一次釋放。

Ckarta 應採 request／connection scoped pool，但必須改成 ck_ 命名，並建立自己的 lifetime 契約。

證據：

https://nginx.org/en/docs/dev/development_guide.html
https://github.com/nginx/nginx/blob/master/src/core/ngx_palloc.c

## 4. Nginx timer

Nginx 使用 global timer red-black tree（全域計時器紅黑樹），event object 含 timer node。

Ckarta 初版採相同複雜度特性的 timer tree 是合理選擇。

若日後大量 timer 成為瓶頸，可研究 timing wheel（時間輪），但不得在沒有 benchmark 前宣稱更快。

學術來源：

George Varghese and Tony Lauck,
"Hashed and Hierarchical Timing Wheels: Data Structures for the Efficient Implementation of a Timer Facility"

DOI:
https://doi.org/10.1145/41457.37504

## 5. Nginx upstream hot path

已核對：

src/http/ngx_http_upstream.c

實際檔案存在，且包含 upstream state、peer handling（節點處理）、連線與回應流程。

Ckarta 的 proxy path（代理路徑）可以吸收此分層思想：

route
→ upstream selection
→ upstream connection
→ upstream request
→ upstream response
→ output

但不能把 Nginx 的內部資料結構當成 API。

證據：

https://github.com/nginx/nginx/blob/master/src/http/ngx_http_upstream.c

## 6. Tomcat NIO whole path

已核對 Tomcat main branch 的：

java/org/apache/tomcat/util/net/NioEndpoint.java

其中存在：

- Poller
- Selector
- PollerEvent
- SocketProcessor
- NioChannel
- SecureNioChannel

而 startInternal() 啟動 Acceptor／Poller／executor 等元件。

這說明 Tomcat 自己已經把低階 I/O 與工作執行器分層，而不是一條「socket 直接呼叫 Servlet」路徑。

證據：

https://github.com/apache/tomcat/blob/main/java/org/apache/tomcat/util/net/NioEndpoint.java

## 7. Tomcat HTTP protocol layer

已核對：

Http11NioProtocol.java

其建構方式直接使用 NioEndpoint。

因此 Tomcat 的結構可概括為：

HTTP/1.1 protocol
→ NIO endpoint
→ socket processing
→ Coyote request/response
→ Catalina adapter
→ Container

證據：

https://github.com/apache/tomcat/blob/main/java/org/apache/coyote/http11/Http11NioProtocol.java

## 8. Tomcat Coyote → Catalina 邊界

已核對：

CoyoteAdapter.java

此類別處理 Coyote Request/Response 與 Catalina Request/Response 的銜接，也處理 async dispatch 等狀態。

Ckarta 的 JNI bridge 在架構上最接近這個「低階傳輸表示 → Servlet 容器表示」邊界。

但 Ckarta 不應照搬 Java class hierarchy；JNI bridge 必須維持 ownership 與 buffer lifetime。

證據：

https://github.com/apache/tomcat/blob/main/java/org/apache/catalina/connector/CoyoteAdapter.java

## 9. Tomcat Container path

Tomcat StandardContext.java：

Context 是 Container tree 中處理特定 Web application 的核心層之一，並建立 basic Valve。

Tomcat StandardWrapper.java：

Wrapper 表示單一 servlet definition（Servlet 定義），並負責 Servlet instance lifecycle（實例生命週期）、load、init 與 allocation。

因此目標 Java 路徑：

Engine
→ Host
→ Context
→ Wrapper
→ Filter chain
→ Servlet

是有官方原始碼依據的。

證據：

https://github.com/apache/tomcat/blob/main/java/org/apache/catalina/core/StandardContext.java
https://github.com/apache/tomcat/blob/main/java/org/apache/catalina/core/StandardWrapper.java

## 10. Tomcat async path

CoyoteAdapter.java 的 asyncDispatch() 顯示 async request 的 timeout／error 等狀態需要重新進入容器處理。

這對 Ckarta 有重大影響：

Servlet AsyncContext 不能設計成「Java 回傳後 request 一定完成」。

C connection state 必須能：

SERVED
→ ASYNC_WAIT
→ completion/timeout/error
→ WRITE/CLOSE

因此 connection lifetime 與 Servlet async lifetime 必須可分離。

## 11. Nginx 與 Tomcat 的真正差異

Nginx 的核心優勢：

event-driven（事件驅動）
+
worker process
+
non-blocking I/O
+
memory pool
+
資料平面高度 C 化

Tomcat 的優勢：

Servlet container semantics
+
Java object lifecycle
+
Container hierarchy
+
NIO endpoint
+
executor
+
Servlet async lifecycle

兩者不能直接 1:1 疊加。

## 12. Ckarta hot path

推薦 hot path：

accept
→ TLS state machine
→ HTTP header parser
→ request framing validation
→ rate/connection limits
→ route
→ static/proxy/servlet decision

### Static

route
→ file metadata
→ range/conditional handling
→ sendfile or buffered output
→ compression if selected
→ TLS
→ socket

### Proxy

route
→ peer selection
→ upstream connection
→ upstream HTTP parser
→ response filters
→ TLS
→ socket

### Servlet

route
→ JNI request descriptor
→ Java request facade
→ Engine
→ Host
→ Context
→ Wrapper
→ Filter chain
→ Servlet
→ response descriptor
→ JNI
→ C output pipeline
→ TLS
→ socket

## 13. 不應採用的錯誤路徑

### 錯誤 A

C event loop
→ JNI
→ Servlet.service()
→ database call
→ event loop 被阻塞

禁止。

### 錯誤 B

C HTTP parser
→ Java parser
→ upstream parser

可能造成 request framing interpretation mismatch（請求框架解讀不一致）。

禁止在沒有明確協定邊界時重複解析。

### 錯誤 C

C Session store
+
Java Session store

容易造成生命週期與一致性衝突。

Servlet Session 語意保留 Java。

### 錯誤 D

「所有東西 lock-free」

沒有 benchmark 與 memory reclamation（記憶體回收）證據不得採用。

## 14. SEDA 是否採用

SEDA 論文確實提出 staged event-driven architecture（分段事件驅動架構），使用 stage、queue 與資源控制處理高並行網路服務。

Ckarta 可以吸收：

stage isolation（階段隔離）
+
bounded queue（有界佇列）
+
overload control（過載控制）

但 Ckarta 不應宣稱自身是 SEDA implementation（SEDA 實作），除非實際實作其核心模型。

學術來源：

Matt Welsh, David Culler, Eric Brewer,
"SEDA: An Architecture for Well-Conditioned, Scalable Internet Services"

DOI:
https://doi.org/10.1145/502059.502057

## 15. 事件驅動多處理器

事件驅動程式若要利用多處理器，必須控制事件之間的資料相依。

可引用的學術來源：

"Multiprocessor Support for Event-Driven Programs"
USENIX Annual Technical Conference 2003

https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

Ckarta 由 worker ownership 與分片開始，而不是共享全域狀態。

## 16. C10K

C10K 不是 peer-reviewed paper，因此本文件只把它作為歷史／工程背景資料，不把它列為主要學術證據。

來源：
https://kegel.com/c10k.html

## 17. 目前 hot-path 結論

### C 化

明確通過：

- TCP socket handling
- event loop
- HTTP parsing
- TLS
- connection state
- request size checks
- static file
- reverse proxy
- load balancing
- output buffering
- rate limiting
- connection limiting

### Java 保留

明確通過：

- Servlet API
- Servlet lifecycle
- Filter
- Listener
- Session
- ServletContext
- RequestDispatcher
- AsyncContext
- class loading
- web application lifecycle

### 條件化

需要 benchmark／安全分析後決定：

- cache metadata
- compression
- structured logging
- metrics aggregation
- advanced lock-free structures
- timing wheel
- shared-memory cross-worker state

## 18. 後續逐函式研究範圍

實作前應再建立 function-level trace（逐函式追蹤），至少包含：

Nginx：

ngx_process_events_and_timers
→ ngx_process_events
→ Linux epoll backend
→ connection handler
→ HTTP request processing
→ upstream / static / output path

Tomcat：

NioEndpoint.Poller
→ SocketProcessor
→ protocol processing
→ CoyoteAdapter
→ Container pipeline
→ Wrapper／Servlet invocation
→ async dispatch

逐函式研究的目的不是複製程式碼，而是建立：

- ownership map
- state transition map
- allocation map
- blocking points
- lock／atomic points
- JNI crossing candidates
- error propagation
- cancellation points

## 19. 基線聲明

本文件所有「存在」的原始碼路徑都應以其官方 repository 目前可查版本為準。

未在本文件明確列出的函式，不得在後續文件中宣稱已核對。
