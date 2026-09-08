# Ckarta CGI／FastCGI 介面可行性研究

## 1. 結論

Ckarta 可以增加 CGI 介面，但應視為 C Web server data plane 的外部 application gateway（應用程式閘道），而不是 Servlet container 的替代執行模型。

建議分成兩種能力：

1. CGI/1.1：提供給傳統或自行撰寫的外部 C／其他可執行程式，定位為相容性與整合功能。
2. FastCGI：作為 PHP 等長生命週期應用程式的主要 gateway；PHP 官方目前將 PHP-FPM 描述為主要的 PHP FastCGI 實作，並列出其適合高負載網站的管理能力。

來源：
RFC 3875：https://www.rfc-editor.org/info/rfc3875
PHP-FPM：https://www.php.net/manual/en/book.fpm.php

## 2. CGI/1.1 的本質

RFC 3875 將 CGI 定義為 HTTP server 與外部程式之間的介面。server 負責 connection、data transfer、transport 與 network；CGI script 負責 application。

因此 CGI 天然形成：

HTTP server
→ process boundary
→ CGI program
→ stdin/stdout/environment

而不是：

HTTP server
→ in-process function call

RFC 3875 是 Informational RFC，不是 Internet Standard。其文字也明確說明沒有經 IETF 對安全、壅塞控制等事項作完整標準審查。

來源：
https://www.rfc-editor.org/info/rfc3875

## 3. 純 C CGI

理論上最直接：

HTTP request
→ path resolution
→ executable selection
→ create process
→ set CGI environment
→ request body → child stdin
→ child stdout → HTTP response
→ wait/reap
→ release

純 C 程式可以直接編譯成 CGI executable。

優點：

- 不需要 JVM。
- 不需要 PHP。
- process isolation（程序隔離）天然存在。
- 語言不限於 C，只要可以產生相容 CGI 的 executable。

缺點：

- 每次建立程序的成本可能很高。
- process、address space（位址空間）、descriptor、environment（環境）都有額外資源成本。
- high concurrency（高併發）下必須限制 child process 數量。
- child stdout／stderr／stdin 若處理不當可能阻塞 event loop。

因此 Ckarta 不應讓 CGI execution 直接呼叫阻塞式 process wait。

## 4. Nginx 比對

固定 Nginx 1.30.4 不把一般 CGI 作為 core HTTP request execution path；其官方 HTTP FastCGI 模組把請求送往 FastCGI server，並提供 buffering、connect/send/read timeout、request buffering、client abort、upstream retry、cache 等 gateway controls。

固定原始碼：
third_party/nginx/src/http/modules/ngx_http_fastcgi_module.c

官方文件：
https://nginx.org/en/docs/http/ngx_http_fastcgi_module.html

FastCGI module 的 request context 本身保存 state、buffer／chain、record parsing 與 closed/header flags，顯示 gateway 是一個真正的 connection/protocol state machine，而不是單純把 stdout 接到 response。

## 5. Tomcat 比對

固定 Tomcat 11.0.25 的 CGIServlet 是 Servlet container 內的 CGI bridge。

其路徑是：

Servlet request
→ CGIEnvironment
→ CGIRunner
→ external process
→ stdin/stdout/stderr

固定原始碼：
third_party/tomcat/java/org/apache/catalina/servlets/CGIServlet.java

目前版本的實作會透過 Java Runtime process execution 建立 CGI process，並另有 stderr handling。其程式碼註解亦明確指出 CGI 實作有部分限制與仍待改善事項。

因此 Ckarta 若提供 CGI，不必把 CGI 語意複製成 Java Servlet；可以直接在 C data plane 提供獨立 CGI gateway。

## 6. PHP

PHP 官方目前把 PHP-FPM 描述為主要的 PHP FastCGI 實作，並特別包含適合高負載網站的管理功能。

因此推薦：

Ckarta
→ FastCGI
→ PHP-FPM

而不是：

Ckarta
→ 每個 request 建立 php-cgi process

第二方案理論上可做，但會把 process creation cost 與 PHP initialization cost 放進 request critical path，除非 workload 很低或有明確相容性需求，不宜作主要 PHP 路徑。

來源：
https://www.php.net/manual/en/book.fpm.php

## 7. CGI 與 Servlet 的架構位置

Ckarta 可以形成三條 application path：

### Static

C HTTP
→ static file
→ response

### Servlet

C HTTP
→ canonical request
→ bounded semantic handoff
→ Java Servlet container
→ response

### CGI/FastCGI

C HTTP
→ canonical HTTP request
→ application gateway
→ external process／FastCGI server
→ response

CGI/FastCGI 不應進 Java Servlet container，除非未來為了相容性另外實作 Servlet-level CGIServlet。

## 8. Process lifecycle

CGI child 必須具有明確 lifecycle：

SPAWNING
→ RUNNING
→ EXITING
→ REAPING
→ CLOSED

所有狀態都必須綁定 parent request／gateway owner。

禁止：

event loop
→ blocking waitpid／等效同步等待
→ child 完成

應改為：

event loop
→ spawn／register child
→ return to event loop
→ pipe/stdout readiness or process completion event
→ update CGI state
→ response

實際 process completion notification 應使用平台適合的非阻塞機制，第一階段 Linux 實作可以先研究 wait/reaping integration；跨平台 API 必須在實作前確認。

## 9. stdin/stdout/stderr

三條流的 owner／lifetime 必須明確：

stdin：
C request body → child

stdout：
child → C HTTP response parser

stderr：
child → C logging sink

child stderr 不可因為 child 未讀而讓 event loop 永久阻塞。

stdout 不能直接假設等於 HTTP body；CGI protocol 仍有 response header parsing 與 body termination semantics。

## 10. CGI environment

至少需按 RFC 3875 生成：

GATEWAY_INTERFACE
SERVER_SOFTWARE
SERVER_NAME
SERVER_PROTOCOL
SERVER_PORT
REQUEST_METHOD
PATH_INFO
PATH_TRANSLATED
SCRIPT_NAME
QUERY_STRING
REMOTE_ADDR
AUTH_TYPE（適用時）
REMOTE_USER（適用時）
REMOTE_IDENT（適用時）
CONTENT_TYPE（適用時）
CONTENT_LENGTH（適用時）
HTTP_* request header variables（適用時）

實際欄位與例外必須以 RFC 3875、Ckarta HTTP canonicalization 結果與安全政策逐項決定，不得直接照抄某一伺服器的私有環境變數集合。

## 11. 執行檔選擇與命令注入

CGI 路徑解析必須完全與 HTTP path canonicalization 分離並一致。

禁止把未經安全驗證的 URI／query string 直接拼接成 shell command。

優先使用直接 executable + argv + controlled environment，而不是 shell interpretation。

在 Unix-like 平台上，實際使用的 process creation API 與 privilege drop（權限降低）方式必須依平台與 C library 正式宣告確認後才可寫入實作。

## 12. 安全與隔離

CGI 比 Servlet 多一個 OS process attack surface（作業系統程序攻擊面）。

至少需要：

- dedicated execution identity（專用執行身分）
- resource limits
- CPU／wall-clock timeout
- maximum stdin size
- maximum stdout size
- maximum stderr size
- child process count limit
- executable allowlist（允許清單）
- path canonicalization
- environment allowlist
- working-directory policy
- graceful kill → reap
- client disconnect cancellation

不要直接把 Ckarta server 的高權限環境完整傳給 CGI child。

## 13. 與 Nginx 的關鍵差異

Nginx FastCGI：

C HTTP worker
→ upstream protocol
→ separately managed application server

Ckarta CGI：

C HTTP worker
→ OS process lifecycle
→ pipes
→ CGI application

Ckarta 若實作 FastCGI：

C HTTP worker
→ FastCGI client state machine
→ external PHP-FPM／FastCGI server

因此 FastCGI 對 Ckarta 比 CGI process-per-request 更符合高負載 application gateway。

## 14. 學術啟示

Flash 的研究顯示不同 Web server concurrency architectures 在不同 workload 可能有不同最佳結果；這支持 Ckarta 將 CGI 視為受控的 application execution path，而不是要求所有工作共享同一 concurrency model。

來源：
Vivek S. Pai, Peter Druschel, Willy Zwaenepoel,
“Flash: An Efficient and Portable Web Server”,
USENIX ATC 1999.
https://www.usenix.org/conference/1999-usenix-annual-technical-conference/flash-efficient-and-portable-web-server

JAWS 研究同樣把 concurrency strategy、I/O strategy、event dispatch 與 protocol handler 分開，支持 gateway 的 concurrency policy 與 Servlet execution policy 分離。

來源：
https://www.dre.vanderbilt.edu/JAWS/papers/webframeworks.pdf

## 15. 初步產品決策

CGI：
支援，定位為 optional compatibility gateway（可選相容性閘道）。

FastCGI：
支援，優先於 process-per-request CGI 作為 PHP integration（PHP 整合）。

PHP：
不把 PHP interpreter 嵌入 Ckarta；優先對接 PHP-FPM。

純 C：
可透過 CGI executable 或 FastCGI server 整合；native in-process plugin（原生進程內外掛）不是本研究的 CGI 方案。

Servlet：
保持獨立 Java semantic plane。

## 16. 尚未實作

本文件不代表：

- CGI 已實作。
- FastCGI 已實作。
- PHP-FPM 已整合。
- process sandbox 已完成。
- child process security 已完成。

真正實作前必須建立：

CGI state machine
process owner/lifetime model
pipe backpressure
child cancellation
reaping
resource limits
security tests
protocol tests
benchmark


## 17. 「核心無 CGI」與「可掛接模組」效能比較

本比較區分三種狀態，而不是只比較「有沒有 CGI 功能」。

### A. 建置時不包含 CGI/FastCGI 模組

Ckarta 核心 request path 不需保存 CGI/FastCGI 的 configuration（設定）、process state（程序狀態）或 upstream state（上游狀態）。

對非 CGI request：

T_A ≈ T_core

這是最低風險、最低額外狀態與最容易驗證的基線。

### B. 模組已存在／可載入，但 request 沒有命中 CGI/FastCGI route

此情況可能增加：

- startup configuration parsing（啟動設定解析）
- module metadata（模組中繼資料）
- route／phase dispatch（路由／階段分派）
- per-location module configuration（每路徑模組設定）
- module registry bookkeeping（模組登錄管理）

Nginx 1.30.4 的 HTTP 初始化會為 HTTP modules 建立 main／server／location configuration context，並建立 phase engine；因此「模組存在」不等於「每 request 都進入模組 handler」，但會有啟動與記憶體方面的成本。

固定原始碼：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http.c

### C. request 命中 CGI/FastCGI route

此時成本不是模組分派本身，而主要來自 application gateway：

T_C ≈ T_core + T_gateway + T_application + T_output

其中 T_gateway 包含 request parameter generation、protocol framing、connection／pipe handling、buffering 與 failure handling。

對 CGI：

T_gateway 另外包含 process creation、process setup、stdio pipe 等成本。

對 FastCGI：

T_gateway 改成 persistent upstream connection、FastCGI record framing、request／response streaming 與 process pool 的外部管理成本。

## 18. 為什麼 CGI 不應進核心 hot path

若所有 request 都經：

request
→ CGI capability check
→ CGI route decision
→ external gateway state

即使沒有命中 CGI，也可能在 hot path 增加 branch、state access 或 cache footprint。

因此 Ckarta 應採 route-driven optional module：

request
→ normal core routing
→ selected handler/module only when configured

而非把 CGI abstraction（抽象）做成所有 request 都經過的 mandatory layer（強制層）。

這也符合 Nginx phase／location 設計：FastCGI handler 只在對應 location configuration 啟用時進入，未命中者仍沿一般 HTTP core path。

固定 FastCGI handler：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/modules/ngx_http_fastcgi_module.c

## 19. 學術證據的限制

Apte、Hansen、Reeser 的 2003 年 Computer Communications 研究直接比較 CGI、FastCGI、Servlet、JSP，並發現 FastCGI 一般優於 CGI；但其 Servlet／JSP／CGI 結果來自當時具體實作與硬體，不能直接外推到今天的 HotSpot、Nginx 或 Ckarta。

正式書目：

Varsha Apte, Tony Hansen, Paul Reeser, “Performance comparison of dynamic web platforms”, Computer Communications 26(8), 2003, 888–898.

DOI：
https://doi.org/10.1016/S0140-3664(02)00221-9

可閱讀版本：
https://www.cse.iitb.ac.in/~varsha/allpapers/mypapers/web_comparison_journal.pdf

其最有價值的結論不是固定倍率，而是 performance ranking（效能排名）會隨 application complexity（應用程式複雜度）改變。

因此 Ckarta 目前只能建立：

CGI 具有 process creation 的結構性額外成本；
FastCGI 消除每 request process creation；
module not installed 能避免其 per-request module state；
但實際差距必須以 Ckarta 自有 benchmark 驗證。

## 20. 最終產品決策

正式核心：

不包含 CGI request execution。

可掛接模組：

- CGI module：未來可選。
- FastCGI module：未來可選，PHP integration 優先。
- 其他 application gateway：沿用同一 module boundary。

核心 API 應只知道：

route
→ selected application handler

而不應硬編碼：

route
→ CGI
或
route
→ FastCGI。

模組載入、配置與 handler registration 應在 startup/configuration 階段完成；非命中 request 不建立 CGI/FastCGI request state。

這是一個架構原則，不代表現在已經存在 module loader 或 CGI/FastCGI implementation。

## 21. 建議 benchmark

未來至少比較：

1. CGI module not built。
2. CGI module built but disabled。
3. CGI module enabled but route not hit。
4. CGI route hit。
5. FastCGI module enabled but route not hit。
6. FastCGI route hit。
7. Servlet route hit。

每組都應使用相同 HTTP parser、TCP/TLS、request size、response size、keep-alive 與 concurrency，並量測：

- requests/s
- p50／p95／p99 latency
- CPU
- RSS／memory
- allocation／GC
- event-loop utilization
- routing overhead
- gateway queue wait
- process creation／upstream connect cost

不能把「CGI module 存在但沒命中」與「真的執行 CGI」混成同一個 benchmark。
