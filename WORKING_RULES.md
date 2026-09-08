# Ckarta 工作準則

## 0. 文件狀態

狀態：架構基線，已完成專業架構評定。

本文件是 Ckarta repository（儲存庫）的最高優先工作準則之一。任何自動化開發、程式碼產生、重構、效能最佳化、安全修補或架構變更，開始前都必須閱讀本文件與 ARCHITECTURE.md、HOT_PATH_REVIEW.md。

本文件不宣稱 Ckarta 已經實作 Jakarta Servlet 6.1；它只定義目前核准的目標架構與工程約束。

## 1. 專案目標

Ckarta 是以 Jakarta Servlet 6.1 為相容性目標的 Servlet container（伺服端小程式容器）與高效能 Web server（網頁伺服器）。

最低 Java 平台版本：Java SE 17。

JVM 內的 Java 部分負責 Servlet API（伺服端小程式應用程式介面）語意與容器生命週期；C 部分優先負責網路資料平面。

## 2. 規格優先順序

發生衝突時：

1. 適用的 RFC／正式網路標準。
2. Jakarta Servlet 6.1 規格。
3. 官方 API specification（應用程式介面規格）。
4. Ckarta 本文件與架構不變條件。
5. Nginx 官方文件與原始碼。
6. Apache Tomcat 官方文件與原始碼。
7. 可驗證的同儕審查學術文獻。
8. 其他可靠技術資料。

不得以「Tomcat 這樣做」取代 Servlet 規格要求。

## 3. C/Java 邊界

### C 負責

- socket（通訊端）
- event loop（事件迴圈）
- I/O multiplexing（輸入輸出多路複用）
- HTTP parsing（HTTP 解析）
- TLS termination（TLS 終止）
- connection management（連線管理）
- static file serving（靜態檔案傳送）
- reverse proxy（反向代理）
- load balancing（負載平衡）
- response buffering（回應緩衝）
- compression（壓縮）
- rate limiting（速率限制）
- connection limiting（連線限制）
- network-layer access control（網路層存取控制）
- logging（日誌）
- metrics transport（指標傳輸）

### Java 負責

- Servlet API
- ServletContext
- Request／Response 語意
- Filter（過濾器）
- Listener（監聽器）
- RequestDispatcher（請求分派器）
- AsyncContext（非同步內容）
- Session（會話）
- Web application lifecycle（網頁應用程式生命週期）
- class loading（類別載入）
- deployment（部署）
- Java application execution（Java 應用程式執行）

C 不得直接執行任意 Java Web application（網頁應用程式）程式碼。

Servlet application code 不得在 C event-loop thread（事件迴圈執行緒）上執行。

## 4. JNI 邊界

JNI（Java Native Interface，Java 原生介面）是首選的進程內整合方式。

JNI crossing（JNI 邊界穿越）必須粗粒度化；禁止為單一位元組、單一標頭或極小片段反覆呼叫 Java。

Native buffer（原生緩衝區）若被 Java 透過 DirectByteBuffer（直接位元組緩衝區）觀察，其生命週期必須覆蓋所有 Java 使用時間。

每個 native allocation（原生配置）必須具有：

- owner（擁有者）
- lifetime（生命週期）
- length（實際長度）
- capacity（容量）
- release rule（釋放規則）

不得允許 Java 保存已銷毀 C memory pool（記憶體池）的指標。

## 5. C 命名

沿用 Nginx 官方 C 命名精神，但將 nginx 的 ngx_ 字首改為 ck_。

例如：

ngx_event_t -> ck_event_t
ngx_connection_t -> ck_connection_t
ngx_pool_t -> ck_pool_t

Ckarta 不得宣稱這些名稱是 Nginx API。它們是 Ckarta 私有名稱。

## 6. C 排版

縮排只能使用 Tab。

禁止使用空格進行縮排。

大括弧採 Allman style（Allman 風格）。

函式：

static void
ck_example(void)
{
	...
}

控制結構：

if (condition)
{
	...
}
else
{
	...
}

不得在 C 程式碼中引入與此規則衝突的自動格式化設定。

## 7. Java 命名

遵循 Oracle Java Code Conventions（Oracle Java 程式碼慣例）：

- Class（類別）：UpperCamelCase
- method（方法）：lowerCamelCase
- variable（變數）：lowerCamelCase
- constant（常數）：UPPER_CASE_WITH_UNDERSCORES

## 8. 並行模型

C 資料平面：

worker process（工作者程序） + event loop。

Java Servlet 平面：

executor／thread pool（執行器／執行緒池）。

第一優先是 worker ownership（工作者所有權）與資料分片，不是 lock-free（無鎖）。

禁止因「無鎖」名稱而直接採用複雜 lock-free data structure（無鎖資料結構）。

## 9. 非阻塞規則

C event loop 不得執行未知執行時間的阻塞操作。

需要阻塞的作業必須：

- 移入獨立工作執行緒／執行器；
- 或使用明確的非同步機制；
- 或在架構文件中證明它不會阻塞。

Servlet application 不受 C event loop 直接支配。

## 10. HTTP framing（HTTP 訊息框架）

request framing（請求框架）只能有一個規範化解析路徑。

前端 parser（解析器）、proxy parser（代理解析器）與 upstream parser（上游解析器）不得採用互相衝突的訊息長度規則。

所有 Content-Length、Transfer-Encoding、chunked encoding（分塊編碼）、重複標頭與異常訊息框架都必須以適用 RFC 為準。

HTTP request smuggling（HTTP 請求走私）是阻斷式安全需求，不是未來最佳化項目。

## 11. 連線狀態

每個 connection（連線）必須由顯式 state machine（狀態機）表示。

至少涵蓋：

ACCEPTED
TLS
READ_HEADER
READ_BODY
ROUTE
SERVLET
WRITE_HEADER
WRITE_BODY
KEEPALIVE
CLOSED

實際狀態名稱可調整，但生命週期不可依賴隱含控制流程。

## 12. 逾時與資源上限

至少考慮：

- TLS handshake timeout（TLS 握手逾時）
- header read timeout（標頭讀取逾時）
- body read timeout（本文讀取逾時）
- keep-alive timeout（持久連線逾時）
- upstream connect timeout（上游連線逾時）
- upstream response timeout（上游回應逾時）
- AsyncContext timeout
- graceful shutdown timeout（優雅停止逾時）

任何可由遠端輸入無限延長的狀態都必須有資源上限。

## 13. 記憶體

一般 request-scoped temporary data（請求範圍暫態資料）優先使用 C memory pool。

memory pool 不得管理 Java heap object（Java 堆積物件）。

大型資料不得因方便而整批複製到 Java heap。

任何 pool lifetime 都必須有明確 owner。

## 14. 零拷貝

zero-copy（零拷貝）是條件式最佳化，不是所有路徑的保證。

無內容轉換的靜態檔案路徑可使用 sendfile。

需要壓縮、TLS 加密或 Java 應用程式產生內容時，不得宣稱端到端零拷貝。

## 15. 靜態檔案

靜態檔案預設繞過 JVM。

必須防止：

- path traversal（路徑穿越）
- symlink escape（符號連結逃逸）
- path canonicalization mismatch（路徑正規化不一致）
- range abuse（範圍請求濫用）

大型檔案不得無條件整個讀入 heap。

## 16. 代理與負載平衡

初始目標：

- weighted round robin（加權輪詢）
- failure counting（失敗計數）
- timeout
- connection limit
- backup server（備援伺服器）
- upstream connection reuse（上游連線重用）

不得在沒有正確方法語意分析的情況下自動重試非冪等 HTTP request（HTTP 請求）。

## 17. Session

Session 語意由 Java Servlet container 管理。

C 可以協助：

- Cookie transport（Cookie 傳輸）
- session affinity（會話黏著）
- routing metadata（路由資訊）

C 不得建立一個與 Java Session 生命週期互相競爭的第二套 Servlet Session semantics（Servlet 會話語意）。

## 18. 安全

至少涵蓋：

- TLS
- HTTP security headers（HTTP 安全標頭）
- request size limits（請求大小限制）
- rate limits
- connection limits
- timeout
- access control
- request smuggling 防禦
- Slowloris（慢速攻擊）防禦
- buffer overflow（緩衝區溢位）防禦
- integer overflow（整數溢位）檢查
- use-after-free（釋放後使用）防護
- double free（二次釋放）防護
- least privilege（最小權限）

所有外部長度都必須驗證。

所有 parser 優先使用 pointer + length（指標加實際長度）模型。

禁止 gets、strcpy、strcat，以及無界 sprintf 類使用方式。

## 19. TLS

TLS termination 必須在 C data plane（C 資料平面）完成，除非有明確架構例外。

部署基準必須支援現代安全 TLS 配置，並建立 protocol downgrade（協定降級）、invalid handshake（錯誤握手）、certificate validation（憑證驗證）與 session resumption（會話恢復）測試。

TLS private key（私鑰）必須受到作業系統權限保護。

## 20. 最小權限

公開服務不得以 root 身分長期執行。

master process（主程序）、worker process 與管理介面應具有可分離權限。

Servlet application 不得自動取得 C manager（C 管理元件）的管理權限。

## 21. 測試

所有核心模組至少規劃：

- unit test（單元測試）
- integration test（整合測試）
- negative test（負向測試）
- stress test（壓力測試）
- fuzz test（模糊測試）
- shutdown test（停止測試）
- resource exhaustion test（資源耗盡測試）

JNI 邊界必須測試 lifetime、重入、例外與取消。

## 22. 相容性

必須取得並執行 Jakarta Servlet 6.1 TCK（Technology Compatibility Kit，技術相容性套件）。

相容性判定不得只依賴「能跑 Tomcat 應用程式」。

## 23. 效能宣稱

以下詞語都需要實測證據：

- faster（更快）
- lower latency（更低延遲）
- less memory（更少記憶體）
- higher throughput（更高吞吐量）
- more scalable（更可擴展）

benchmark（效能基準測試）至少記錄：

CPU、核心數、作業系統、核心版本、編譯器、JDK、TLS 設定、連線數、請求大小、回應大小、keep-alive、快取狀態、測試版本與 commit。

禁止從 Big-O 複雜度直接推出真實效能結論。

## 24. 學術來源規則

學術來源必須可驗證存在。

引用必須確認：

- 作者
- 標題
- 出版資訊
- DOI 或穩定網址
- 可閱讀位置

無法確認的資料標記「無法確認」，不得引用。

## 25. Nginx/Tomcat 證據規則

每個重大架構決策若宣稱來自 Nginx 或 Tomcat，必須指出：

- 官方文件或 repository
- 實際模組或類別
- 實際檔案路徑
- 若適用，實際關鍵函式或方法
- Ckarta 採用或不採用的原因

## 26. 禁止事項

禁止：

- 虛構 API
- 虛構設定指令
- 虛構原始碼路徑
- 虛構論文
- 虛構 DOI
- 虛構 benchmark
- 虛構安全保證
- 把設計意圖寫成已實作功能
- 把理論可行寫成已驗證可行

## 27. 變更管理

任何架構修改都必須說明：

- 修改原因
- 受影響模組
- 規格影響
- 安全影響
- 效能假設
- 測試計畫

重大變更應更新 ARCHITECTURE.md 與 HOT_PATH_REVIEW.md。

## 28. 英文術語寫法

本專案文件第一次出現英文電腦科學術語時，必須在其後以括弧提供台灣繁體中文翻譯。

例：

event loop（事件迴圈）
non-blocking I/O（非阻塞輸入輸出）
memory pool（記憶體池）
zero-copy（零拷貝）

標準名稱、API 名稱、類別名稱、函式名稱與檔案名稱可以保持官方英文拼法。

## 29. 本基準的定位

本文件是 Ckarta 工程基線，不代表 Nginx、Tomcat、Jakarta EE 或任何學術來源為 Ckarta 背書。

所有第三方實作只能作為證據與設計參考。
