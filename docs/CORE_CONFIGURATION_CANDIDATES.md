# Ckarta 核心設定選項候選研究

本文件只定義「應研究／應規劃」的核心設定候選，不代表已實作或已承諾的公開設定介面。任何真正加入設定檔以前仍需完成語意、預設值、範圍、安全、reload、效能與相容性設計。

## 1. 分類原則

Ckarta 的核心設定應只控制跨多 request、跨 connection 或影響 native runtime topology（原生執行拓撲）的事項。

不應把每個 Servlet 或 application policy（應用程式策略）都升格成 native core configuration。

候選設定優先順序：

P0 = 核心服務不可正常運作前必須決定。
P1 = 安全、資源上限或高負載穩定性的重要控制。
P2 = 可觀測性、效能調校或平台最佳化。
P3 = 可選功能；沒有明確 workload 證據前不加入核心。

## 2. P0：程序與路徑

| 候選 | Apache | Nginx | Tomcat | Ckarta 判定 |
|---|---|---|---|---|
| runtime/config root | ServerRoot | prefix | CATALINA_BASE | P0；應統一成 Ckarta runtime root |
| main config path | -f | -c | -config | P0；保留 -c |
| pid path | PidFile | pid | 由啟動系統／程序管理 | P1 |
| error log | ErrorLog | error_log | logging.properties 等 | P0 |
| JVM class path | 無對等核心項 | 無對等核心項 | classloader/catalina paths | P0；C 啟動 JVM 必須知道 |
| working directory | CoreDumpDirectory 等 | working_directory | CATALINA_BASE／啟動環境 | P1 |

結論：P0 要先完成 runtime root、config path、log destination、JVM class path；不要以 process current working directory（程序目前工作目錄）作為長期資源根目錄。

## 3. P0/P1：listener 與連線

候選：

- listen address
- listen port
- backlog
- socket reuse policy
- connection limit
- keep-alive timeout
- maximum requests per connection
- header timeout
- body read timeout
- write/send timeout
- maximum request-line size
- maximum header count
- maximum header-field size
- maximum request body size

Nginx 的 ngx_http_core_module 明確把 listen、client_header_timeout、client_body_timeout、client_max_body_size、large_client_header_buffers、keepalive_timeout、keepalive_requests、send_timeout 等作為 HTTP core directives。

Tomcat 的 AbstractEndpoint/AbstractProtocol 與 HTTP Connector 對應提供 port、acceptCount、maxConnections、maxThreads、maxQueueSize、connectionTimeout、keepAliveTimeout、maxHeaderCount 等。

Apache core 以 LimitRequestLine、LimitRequestFields、LimitRequestFieldSize、KeepAliveTimeout 等限制 request／connection；event/worker MPM 另以 MaxRequestWorkers 管制併行服務量。

Ckarta 應把這些概念納入核心候選，但不能直接複製三套名稱或預設值。

## 4. P1：並行與背壓

候選：

- C worker count
- per-worker connection limit
- Java executor maximum threads
- Java executor queue capacity
- C→Java dispatch queue capacity
- Java→C completion queue capacity
- overload policy
- shutdown/drain timeout

Apache event MPM 以 MaxRequestWorkers、ThreadsPerChild、ServerLimit、ThreadLimit 等形成容量關係；Tomcat Connector/Executor 以 maxThreads、maxConnections、maxQueueSize、minSpareThreads 等形成另一種容量模型；Nginx 則以 worker_processes、worker_connections 與 event backend 形成不同模型。

Ckarta 不應同時暴露三套可互相矛盾的容量參數。應優先定義少數「邏輯容量」參數，再由 C/Java runtime 推導內部數值。

## 5. P1：資源與記憶體

候選：

- request header memory ceiling
- request body in-memory threshold
- per-connection memory ceiling
- global native memory budget
- per-worker native memory budget
- file descriptor limit
- temporary file directory
- response buffering budget

Nginx 有 connection_pool_size、client_body_buffer_size、client_body_temp_path、output_buffers 等；Apache 有相關 request limits 與 process/thread 資源限制；Tomcat 以 Connector／Endpoint 的 header、body、socket 與 executor 屬性控制容器資源。

Ckarta 應優先讓「硬上限」可驗證，而不是提供大量微調參數。

## 6. P1：TLS

候選：

- TLS enable
- certificate/key material reference
- protocol version policy
- cipher policy
- client certificate policy
- session/reuse policy
- handshake timeout
- SNI host mapping

TLS 是 Ckarta C data plane 的責任，但憑證與私密金鑰生命週期必須與 configuration snapshot 明確分離；設定快照只能持有經驗證的 resource reference（資源參照）。

Tomcat 將 SSLHostConfig 與 Connector 組合；Nginx 以 server/listen/SSL 相關模組組合。兩者都顯示 TLS 設定不能只是一個 boolean。

## 7. P1：安全與異常請求

候選：

- invalid header policy
- request smuggling strictness
- maximum header count
- maximum request-line length
- maximum body
- slow-client timeout
- connection rate limit
- per-IP connection limit
- response rate limit
- access log policy

Nginx 的 client_header_timeout、client_body_timeout、large_client_header_buffers、limit_rate、lingering_close 等直接位於 HTTP core。Apache 以 LimitRequest* 與 KeepAliveTimeout 等控制異常 request 資源。Tomcat Connector 提供 maxHeaderCount、maxPostSize、maxSwallowSize、connectionTimeout 等。

這些應視為 core safety baseline（核心安全基線），不是 optional tuning（可選調校）。

## 8. P2：檔案與靜態資料

候選：

- document root
- sendfile enable
- direct I/O policy
- open-file cache
- file cache size
- static response buffer budget

Nginx 提供 sendfile、directio、open_file_cache、output_buffers；Apache 以 APR／平台能力提供類似路徑；Tomcat 的 default servlet 也有靜態檔案服務能力。

Ckarta 第一階段不應把所有檔案最佳化開關全部暴露；應先有 secure static file service，再透過 benchmark 決定哪些 knobs（調整項）值得公開。

## 9. P2：代理與上游

候選：

- upstream connection limit
- upstream connect timeout
- upstream read timeout
- upstream keepalive
- load balancing policy
- retry policy
- failure threshold
- circuit/open interval

Nginx upstream 模組提供完整的 upstream connection／failure／balancing 設定；Tomcat Connector 並非同一層級，通常只負責入站 HTTP connector。

Ckarta 應讓代理模組自行擁有大部分 upstream configuration；只將跨模組的 connection/resource ceilings 留在 core。

## 10. P2：記錄與觀測

候選：

- error log level
- access log enable
- log format selection
- request correlation enable
- metrics enable
- metrics destination

Apache、Nginx、Tomcat 都有完整 logging／diagnostics configuration，但其資料模型不同。

Ckarta 應避免讓 access log schema 直接綁死 Servlet API object model。

## 11. P3：平台調校

暫列未實作：

- CPU affinity
- NUMA policy
- timer granularity
- busy polling
- kernel-specific socket options
- event backend override
- allocator tuning

Nginx 與 Apache 都有部分平台／worker 調校能力，但 Ckarta 尚未有足夠 benchmark 證據決定公開 API。

## 12. 明確禁止先加入 core 的選項

以下目前不列入 core config：

- CGI/FastCGI 選項
- PHP-specific options
- Servlet-specific deployment options
- arbitrary Java system properties
- arbitrary JVM options
- native module arbitrary constructor arguments
- application-specific environment values
- every possible kernel socket knob

理由：這些會把 core configuration 變成無法驗證的「萬用參數傳遞器」。

## 13. 理論上的容量模型

至少需要區分：

C = concurrent native connections
J = concurrently executing Java tasks
Q = queued Java tasks
M = native memory budget

需要滿足的最基本安全條件是：

Q ≤ Q_max
J ≤ J_max
M ≤ M_max
C ≤ C_max

而且 upstream queue、C→Java queue、Java executor queue、completion queue 不能各自無限制，否則整體只是在不同位置累積 backpressure。

因此未來設定 API 應優先以：

connection capacity
application execution capacity
memory capacity

這三個邏輯層次建模，而不是暴露大量 implementation-specific counters。

## 14. 決策

本研究結果只進入未做列表：

P0：
runtime root、config path、logging destination、listen endpoints、JVM class path。

P1：
connection/resource/security/TLS limits、C/Java execution capacity 與 queue limits。

P2：
static file、proxy/upstream、observability。

P3：
platform-specific tuning。

截至本文件版本，以上全部仍為未實作候選。
