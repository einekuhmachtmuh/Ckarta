# Ckarta 架構基線

## 1. 專業評定結論

架構評定：通過，條件為以下邊界與驗證要求不得被後續開發削弱。

本文件描述 target architecture（目標架構）；目前 repo 的實際 executable scope、已驗證程度與未完成項目以 `docs/WORK_STATE.md` 為準，本文件中的「推薦／規劃」不得解讀為已實作能力。

核心判斷：

Ckarta 不應成為「C 版 Tomcat」，也不應成為「Nginx 加 JNI」。

推薦模型是：

Nginx 式 C 事件驅動資料平面
+
Tomcat／Jakarta Servlet 式 Java 語意平面
+
極薄、具有明確 ownership（所有權）與 lifecycle（生命週期）契約的 JNI 語意交接。

這個模型符合 Jakarta Servlet 6.1 允許 Servlet container 與 Web server 位於同一程序或不同程序的架構彈性；Ckarta 選擇同程序 JNI 是效能導向的工程取捨，而非規格強制要求。

## 2. 目標分層

### C 資料平面

ck_master
→ ck_worker
→ ck_event
→ ck_connection
→ ck_http
→ ck_tls
→ ck_router
→ ck_static
→ ck_proxy
→ ck_limit
→ ck_cache
→ ck_output

### Java Servlet 平面

Java bootstrap
→ Servlet container
→ Engine
→ Host
→ Context
→ Wrapper
→ Filter chain
→ Servlet

### JNI

C request metadata + native buffers
↔ JNI bridge
↔ Java request/response facade

JNI 不是第三個業務層；它只是一個邊界。

## 3. 主要資料流

### 動態 Servlet request（請求）

client（客戶端）
→ TCP
→ TLS
→ C event loop
→ HTTP parser
→ security checks（安全檢查）
→ routing（路由）
→ JNI
→ Java container
→ Pipeline
→ Filter chain
→ Servlet
→ Java response
→ JNI
→ C output pipeline
→ TLS
→ TCP

### 靜態檔案

client
→ TCP/TLS
→ C event loop
→ HTTP parser
→ security
→ path resolution（路徑解析）
→ file
→ sendfile（必要時）
→ TLS/output
→ client

此路徑不進 JVM。

### 反向代理

client
→ C frontend
→ HTTP parser
→ route
→ upstream connection pool（上游連線池）
→ upstream HTTP
→ C response path
→ client

## 4. 並行模型

### C

推薦：

worker process + one or more event loops。

Linux 後端優先 epoll。

BSD/macOS 類系統可使用 kqueue。

跨平台抽象必須隔離作業系統 API。

### Java

Servlet application 使用 Java executor/thread pool。

Servlet application 不得阻塞 C event loop。

### 跨邊界

C event loop：

1. 收到可讀／可寫事件。
2. 推進 connection state machine。
3. 完成可以在 C 中快速完成的工作。
4. 依 route 選擇 Servlet 或 optional application gateway module；未啟用／未命中 CGI/FastCGI 時不得建立其專用 request state。
5. Servlet 工作透過有界 semantic handoff 提交 Java executor。
6. 立即回到 event loop；不得等待 Servlet application code。
7. Java completion／async lifecycle event 到達後再推進 C connection state。

## 5. 為什麼不是全部使用 C

Servlet 6.1 的規格定義的是 server-side API（伺服器端應用程式介面）與 container semantics（容器語意），不只是 HTTP parsing。

Session、Filter、Listener、RequestDispatcher、AsyncContext、ServletContext、class loading 與 application lifecycle 都具有 Java API 與 JVM 生命週期語意。

因此：

「HTTP 已經在 C 解析」
不等於
「Servlet container 可以在 C 完成」。

## 6. 為什麼主要使用 JNI

可選邊界：

C → IPC → Java

C → AJP → Java

C → HTTP → Java

C → JNI → Java

主要路徑採 JNI，原因是：

- 避免每個 Servlet request 重新通過程序間協定。
- 避免額外 serialization（序列化）與 parsing。
- 可以使用 DirectByteBuffer。
- 可以使 C buffer 與 Java request/response facade 保持較近的生命週期。

但 JNI 不是「零成本」。設計必須減少 crossing 次數，而不是追求最大 crossing 次數。

## 7. 記憶體所有權

推薦：

server lifetime
→ worker lifetime
→ connection lifetime
→ request lifetime
→ subrequest lifetime

不同生命週期的物件不得共用不受控制的 owner。

request pool 被銷毀時：

- C 暫態請求資料必須失效；
- Java 不得保留對該資料的長期引用；
- 尚未完成的 asynchronous operation 必須改用更長生命週期的 owner。

## 8. C 化決策準則

對功能 F 建立評分：

S_C = 3P + 2I + 2M - 3J - 4A - 4R

其中：

P = performance criticality（效能關鍵度）
I = I/O intensity（輸入輸出密集度）
M = native memory controllability（原生記憶體可控度）
J = JVM interaction cost（JVM 互動成本）
A = API/Servlet semantic dependence（API／Servlet 語意依賴）
R = security risk of native implementation（原生實作安全風險）

這只是 decision aid（決策輔助），不是數學定理。

原則：

- 高 P/I/M、低 A/J/R：優先 C。
- 高 A：留 Java。
- 高 R：除非 C 的收益非常明確，否則留在 Java。
- 高 J：避免細粒度 JNI。

## 9. 核心資料結構

C：

- connection object（連線物件）
- explicit state machine
- request pool
- buffer chain（緩衝鏈）
- timer tree（計時器樹）
- worker-local hash table（工作者區域雜湊表）
- upstream peer set（上游節點集合）

Java：

- Container tree（容器樹）
- Servlet mapping table（Servlet 對映表）
- Filter chain
- Session store
- ClassLoader graph（類別載入器圖）

跨邊界：

- request descriptor（請求描述元）
- response descriptor（回應描述元）
- direct buffer view（直接緩衝區視圖）
- completion record（完成記錄）

## 10. C Pipeline 與 Java Pipeline

### C

HTTP decode
→ security
→ route
→ static/proxy/servlet
→ output filters
→ TLS

### Java

Container
→ Engine
→ Host
→ Context
→ Wrapper
→ Filter Chain
→ Servlet

C 不得繞過 Java Servlet lifecycle。

Java 不直接擁有 C socket。

## 11. 壓力與過載

C 與 Java 兩側都必須有 bounded resource（有界資源）。

至少：

connection limit
request rate limit
request body limit
header limit
worker queue limit
Servlet executor limit
upstream connection limit

不可讓任何網路請求路徑無限增長。

## 12. 管理平面

管理介面必須與公開 HTTP data plane（資料平面）分離。

管理功能至少規劃：

- configuration reload（設定重載）
- worker status（工作者狀態）
- connection statistics（連線統計）
- upstream statistics（上游統計）
- JVM／Servlet statistics（JVM／Servlet 統計）
- graceful shutdown

## 13. 觀測性

這是原始需求沒有明確列出、但專業評定要求補上的部分。

至少提供：

- structured logging（結構化日誌）
- metrics（指標）
- request correlation ID（請求關聯識別碼）
- active connection count（活躍連線數）
- queue depth（佇列深度）
- request latency（請求延遲）
- upstream latency（上游延遲）
- Servlet execution latency（Servlet 執行延遲）
- error counters（錯誤計數）

觀測路徑不得讓慢速日誌 I/O 阻塞 event loop。

## 14. 配置模型

配置應分成：

bootstrap configuration（啟動設定）
→ immutable runtime configuration（不可變執行期設定）
→ reloadable configuration（可重載設定）

Nginx 的 cycle／pool 設計顯示，避免任意 global state（全域狀態）有助於安全重載。

Ckarta 不應把大量設定保存在不可版本化的 global variable。

## 15. 第三方函式庫

C 原生依賴必須逐一記錄：

- upstream project（上游專案）
- version
- license
- CVE／安全公告監測方式
- ABI compatibility（ABI 相容性）
- memory ownership rules

特別注意 TLS、壓縮與正規表示式相關函式庫。

## 16. 協定範圍

第一階段必須明確分開：

- Servlet 6.1 compatibility
- HTTP/1.1
- TLS
- static file
- reverse proxy

HTTP/2 與 HTTP/3 應作為獨立協定專案，不得因「HTTP server」四字就假設第一版已涵蓋。

## 17. 零拷貝範圍

靜態檔案：

file → kernel sendfile path

可能採 zero-copy。

Servlet 動態內容：

application → JVM buffer → JNI → C → TLS

不得保證零拷貝。

TLS 與 compression 也可能要求資料經 CPU 處理。

## 18. 安全邊界

C 的 native parsing 是最高風險區。

因此 parser 優先採：

pointer + length
checked arithmetic
explicit bounds

每個 parser function 都必須可回答：

- input base
- input length
- consumed length
- ownership
- failure behavior

失敗時應採 fail closed（失敗關閉）策略。

## 19. 理論限制

Ckarta 即使採用比 Tomcat 更低階的資料平面，也不能保證：

- 所有 Servlet workload 更快；
- 所有情況記憶體更少；
- 所有 TLS workload 更快；
- 所有應用程式無阻塞；
- 端到端零拷貝；
- 安全性天然高於既有成熟伺服器。

這些都必須由測試證明。

## 20. 專業評定的必要補充

使用者原始需求未明確列出、但本專案必須補上的工程項目：

1. Servlet 6.1 TCK 驗證。
2. HTTP/1.1 framing 的單一權威解析器。
3. 配置熱重載與 immutable configuration。
4. 觀測性與 request correlation。
5. 供應鏈與第三方函式庫版本治理。
6. 明確的管理平面。
7. 有界佇列與資源耗盡策略。
8. fuzzing。
9. TLS interoperability。
10. graceful shutdown。
11. HTTP/2／HTTP/3 明確列為獨立範圍。
12. benchmark 的可重現性。
13. native memory sanitizer／instrumentation 策略。
14. Servlet async 與 C connection lifetime 的取消語意。
15. license compatibility（授權相容性）。

這些不是額外裝飾，而是 C／Java 混合伺服器真正進入可驗證工程狀態所需的基礎。

## 21. Exception/error handling boundary

例外與錯誤處理採分層模型：C native status、JNI pending exception、Java Servlet Throwable、HTTP response outcome、cancellation/timeout 與 process-fatal state 不得混成單一通道。跨層只傳遞穩定 category/code/status 與必要 correlation identity；exactly-once terminal transition 與 owner/lifetime cleanup 必須在 request/connection state machine 中可證明。

完整研究與實作閘門見 `docs/EXCEPTION_HANDLING_RESEARCH.md`；Ckarta 自有函式流程見 `docs/CKARTA_FUNCTION_FLOW.md`。

## 22. 主要證據

Jakarta Servlet 6.1：
https://jakarta.ee/specifications/servlet/6.1/
https://jakarta.ee/specifications/servlet/6.1/jakarta-servlet-spec-6.1.html

Nginx development guide：
https://nginx.org/en/docs/dev/development_guide.html

Nginx event core：
https://github.com/nginx/nginx/blob/master/src/event/ngx_event.c

Nginx memory pool：
https://github.com/nginx/nginx/blob/master/src/core/ngx_palloc.c

Nginx upstream：
https://github.com/nginx/nginx/blob/master/src/http/ngx_http_upstream.c

Tomcat NIO endpoint：
https://github.com/apache/tomcat/blob/main/java/org/apache/tomcat/util/net/NioEndpoint.java

Tomcat HTTP/1.1 NIO protocol：
https://github.com/apache/tomcat/blob/main/java/org/apache/coyote/http11/Http11NioProtocol.java

Tomcat Coyote adapter：
https://github.com/apache/tomcat/blob/main/java/org/apache/catalina/connector/CoyoteAdapter.java

Tomcat StandardContext：
https://github.com/apache/tomcat/blob/main/java/org/apache/catalina/core/StandardContext.java

Tomcat StandardWrapper：
https://github.com/apache/tomcat/blob/main/java/org/apache/catalina/core/StandardWrapper.java

RFC 9112：
https://www.rfc-editor.org/rfc/rfc9112.html

SEDA：
https://doi.org/10.1145/502059.502057

## 22. 理論架構修正

Little 定律 L = λW 顯示每個新增的跨層階段都可能增加服務時間與等待時間；因此 Ckarta 不以「把更多工作搬到 C」作為目標，而以最小化跨界次數、跨界資料量與有界排隊為目標。

Servlet 6.1 的 AsyncContext 與 non-blocking I/O 使 Java application 可以把等待與 request execution 分離；因此 C 資料平面應負責高密度 I/O／connection scheduling，而 Java 保留 Servlet semantics。完整理論分析見 docs/WEB_SERVER_THEORY_SERVLET_NGINX.md。


## 23. Servlet 6.1 中立批判後的相容性方向

Ckarta 的 Servlet 6.1 目標不變，但規格只約束 application-facing API 與 container semantics，不應支配整個 Web server 的內部 execution architecture。

允許的內部分離：

C event-driven network data plane
→ bounded semantic handoff
→ Java Servlet semantic plane

Servlet 6.1 的 Request／Response、Filter、Listener、Session、RequestDispatcher、AsyncContext 與 non-blocking I/O 語意必須保持；其 Java object model、blocking request style 與 callback 實作方式不必成為 C 資料平面的內部表示。
