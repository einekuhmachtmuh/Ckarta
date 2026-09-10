# Ckarta 固定版本逐函式 Hot Path 追蹤

## 1. 版本基準

Nginx：

- stable 1.30.4
- commit 017cf98dcce217946572a896f0992370475e189f
- repository https://github.com/nginx/nginx

Apache Tomcat：

- 11.0.25
- commit cbe6e15ee81e2fc6232954292a80cca5d1e84009
- repository https://github.com/apache/tomcat

本文件只把已從這兩個精確 commit 讀取並確認的 upstream 函式列入「已核對」；Ckarta 自有函式與實作契約另見 `docs/CKARTA_FUNCTION_FLOW.md`。

## 2. Nginx 核心事件 Hot Path

### 2.1 Event loop（事件迴圈）

檔案：
third_party/nginx/src/event/ngx_event.c

函式：
ngx_process_events_and_timers()

在固定版本的實作中，其主要順序是：

1. 計算 timer（計時器）等待值。
2. 處理 accept mutex（接受互斥鎖）狀態。
3. 呼叫 ngx_process_events()。
4. 處理 posted accept events（已排程接受事件）。
5. 釋放 accept mutex。
6. expire timers（使到期計時器失效）。
7. 處理 posted events。

這是 Ckarta event loop 的第一個研究基線。

### 2.2 Linux epoll path

檔案：
third_party/nginx/src/event/modules/ngx_epoll_module.c

函式：
ngx_epoll_process_events()

固定版本中直接呼叫：
epoll_wait()

然後：

- 取得 event_list entry。
- 驗證 file descriptor／event instance。
- 處理 EPOLLERR／EPOLLHUP。
- 對 EPOLLIN 建立 read readiness（讀取就緒）。
- 可選擇將事件加入 posted event queue。
- 交由對應 connection/event handler（連線／事件處理器）處理。

Ckarta 應抽象 event backend（事件後端），但不應把 epoll 語意硬編碼成跨平台 API。

## 3. Nginx HTTP request path

檔案：
third_party/nginx/src/http/ngx_http_request.c

### 3.1 連線初始化

ngx_http_init_connection()

固定版本約第 205 行開始。

實作會：

- 由 connection pool 建立 ngx_http_connection_t。
- 設定 server configuration context（伺服器設定內容）。
- 設定 log context。
- 設定 read handler。
- 若啟用 SSL，將 read handler 設為 ngx_http_ssl_handshake。

因此 Ckarta 的 connection object 必須先於 HTTP request object。

### 3.2 等待請求

ngx_http_wait_request_handler()

固定版本約第 371 行。

其實作會：

- 檢查 timeout。
- 建立 request header buffer。
- 呼叫 c->recv()。
- 若 NGX_AGAIN，設定 client_header_timeout 並重新註冊讀事件。
- 收到資料後建立 request。
- 將 handler 切換到 ngx_http_process_request_line()。

重要結論：

Slowloris 防禦與 event handler 是同一條資料路徑的一部分，而不是獨立後處理。

### 3.3 Request line

ngx_http_process_request_line()

固定版本約第 1109 行。

其實作依序包括：

- read request header
- ngx_http_parse_request_line
- URI processing
- Host validation
- virtual server selection
- HTTP version 判定
- 初始化 request header list
- 轉交 ngx_http_process_request_headers

這個函式是 Ckarta HTTP parser 狀態機設計的重要參考。

### 3.4 Request headers

ngx_http_process_request_headers()

固定版本約第 1395 行。

實作會：

- 檢查 timeout。
- 在 header buffer 不足時配置大型 header buffer。
- 讀取 request header。
- 呼叫 ngx_http_parse_header_line。
- 檢查 invalid header。
- 檢查 max_headers。
- 配置 lowercase header name。
- 以 headers_in_hash 對已知 header 做處理。
- 在 HEADER_DONE 後呼叫 ngx_http_process_request_header。
- 然後呼叫 ngx_http_process_request。

因此 request size、header count、header syntax 與 routing preparation 都位於實際 request parser 路徑。

### 3.5 Request processing

ngx_http_process_request()

固定版本約第 2098 行。

其 HTTPS 分支會檢查：

- HTTP 封包是否錯送至 HTTPS port。
- client certificate verification。
- OCSP status（若配置）。

確認安全條件後：

- 移除 request read timer。
- 將 read/write handler 設為 ngx_http_request_handler。
- 呼叫 ngx_http_handler。

### 3.6 HTTP phase engine

檔案：
third_party/nginx/src/http/ngx_http_core_module.c

ngx_http_handler()

固定版本約第 841 行。

其後設定：
r->write_event_handler = ngx_http_core_run_phases

並直接呼叫：
ngx_http_core_run_phases()

該函式使用 phase_engine.handlers，在 while 迴圈中依 checker（檢查器）推進 HTTP phase。

因此 Ckarta 的 route／security／handler pipeline 不應只是巨大 if/else，而應有明確 phase state。

## 4. Nginx memory pool path

檔案：
third_party/nginx/src/core/ngx_palloc.c

固定版本已確認：

ngx_destroy_pool()
ngx_palloc()
ngx_palloc_small()
ngx_palloc_block()
ngx_palloc_large()
ngx_pool_cleanup_add()

小型 allocation 可由 pool block 處理，大型 allocation 另有 large list。

Ckarta 應採類似「生命週期歸屬」思想，但必須重新定義自己的 ownership。

## 5. Tomcat NIO path

檔案：
third_party/tomcat/java/org/apache/tomcat/util/net/NioEndpoint.java

固定版本已確認：

Poller.run()
Poller.processKey()
SocketProcessor.doRun()

### 5.1 Poller.run()

約第 1063 行。

其主要流程：

- events()
- selector.selectNow() 或 selector.select()
- process selected keys
- 對每個 attachment 呼叫 processKey()
- timeout()

這與 Nginx 的 event loop 在概念上相似，但不是同一個實作模型。

### 5.2 Poller.processKey()

約第 1129 行。

它會：

- 驗證 socket 狀態。
- 處理 readable／writable。
- 若有 sendfile data 則走 sendfile。
- 否則處理 read operation／blocking read state。

重要差異：

Tomcat 的 NIO 層本身已經具有 blocking operation 與 application processor 的橋接，而 Ckarta 必須更嚴格禁止 C event loop 進入未知阻塞工作。

### 5.3 SocketProcessor.doRun()

約第 2152 行。

固定版本會：

- 檢查 TLS handshake 狀態。
- 必要時呼叫 socket handshake。
- handshake 完成後呼叫 handler.process(socketWrapper, event)。
- 若狀態為 CLOSED 則關閉 socket。

因此 TLS 不應在 Ckarta 架構中被當成 HTTP parser 前的一個完全無狀態函式；它本身具有 connection lifecycle。

## 6. Tomcat HTTP/1.1 path

檔案：
third_party/tomcat/java/org/apache/coyote/http11/Http11Processor.java

service()

約第 260 行。

固定版本主要循環：

- parseRequestLine()
- prepareRequestProtocol()
- parseHeaders()
- prepareRequest()
- getAdapter().service(request, response)

這說明 Tomcat 的 protocol processing 與 Servlet container 是兩個不同抽象層。

## 7. Coyote → Catalina path

檔案：
third_party/tomcat/java/org/apache/catalina/connector/CoyoteAdapter.java

service()

約第 305 行。

固定版本：

1. 建立／取得 Catalina Request／Response。
2. postParseRequest。
3. 設定 async support。
4. 呼叫 Container Pipeline 第一個 Valve 的 invoke()。
5. 若 async，保留 request 生命周期。
6. 否則 finishRequest／finishResponse。

這個位置是 Ckarta JNI bridge 最重要的語意參照點。

## 8. Container → Servlet path

檔案：
third_party/tomcat/java/org/apache/catalina/core/StandardWrapperValve.java

invoke()

約第 72 行開始。

固定版本：

- 檢查 Context availability。
- 檢查 Wrapper availability。
- wrapper.allocate()
- ApplicationFilterFactory.createFilterChain()
- filterChain.doFilter()
- 最終進入 Servlet.service()

所以 Ckarta 必須把：

request decode

與

Servlet invocation

視為不同層級。

## 9. Nginx vs Tomcat 關鍵交叉比較

| 問題 | Nginx 1.30.4 | Tomcat 11.0.25 | Ckarta 決策 |
|---|---|---|---|
| I/O demultiplexing | epoll 等事件後端 | Java NIO Selector | C event backend |
| 連線處理 | C event handler | SocketWrapper／Processor | C connection state machine |
| HTTP parsing | C parser | Http11Processor | C parser |
| Servlet semantics | 不適用 | Catalina | Java |
| filter／phase | phase handlers／filters | Container Valve／Filter Chain | C 與 Java 各自有 pipeline |
| memory lifetime | pool | JVM GC + object lifecycle | C pool + Java heap |
| TLS | Nginx SSL path | SecureNioChannel／handshake | C TLS |
| application execution | C module handler | Java executor／Servlet | Java executor |
| async request | C event callbacks | AsyncContext + adapter async dispatch | C state + Java AsyncContext |

## 10. 學術依據與設計限制

### SEDA

Matt Welsh、David Culler、Eric Brewer：

"SEDA: an architecture for well-conditioned, scalable Internet services"

ACM SIGOPS Operating Systems Review 35(5), 2001, 230–243。

DOI：
https://doi.org/10.1145/502059.502057

SEDA 支持：

- staged event-driven architecture（分段事件驅動架構）
- explicit queues（明確佇列）
- overload control（過載控制）

Ckarta 只採其「階段與過載控制」思想，不宣稱 Ckarta 是 SEDA。

### Capriccio

Rob von Behren、Jeremy Condit、Feng Zhou、George C. Necula、Eric Brewer：

"Capriccio: scalable threads for internet services"

SOSP 2003。

DOI：
https://doi.org/10.1145/945445.945471

其研究說明高並行網路服務不必然只有 event-driven model；thread-based model 也可以具有高擴展性。

因此 Ckarta 將 Java Servlet execution 保留在 executor／thread pool，並不是因為事件模型「一定比較差」，而是因為 Servlet application execution 與低階 socket event processing 的責任不同。

### Timing Wheels

George Varghese、Tony Lauck：

"Hashed and Hierarchical Timing Wheels: Data Structures for the Efficient Implementation of a Timer Facility"

DOI：
https://doi.org/10.1145/41457.37504

Timing wheel 可作為未來 timer scalability（計時器擴展性）研究方向；未經 benchmark 不把它列為 Ckarta 預設實作。

### Lock-free hash table

Maged M. Michael：

"High performance dynamic lock-free hash tables and list-based sets"

SPAA 2002。

DOI：
https://doi.org/10.1145/564870.564881

這項研究不能推導「Ckarta 所有共享資料都應 lock-free」。

相反地，它強調 lock-free 結構與 memory management（記憶體管理）彼此相關。

因此 Ckarta 優先使用 worker ownership 與 sharding，再針對證明存在的瓶頸考慮 lock-free。
