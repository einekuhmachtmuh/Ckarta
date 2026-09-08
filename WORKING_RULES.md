# Ckarta 工作準則

本文件是 Ckarta 自動化與人工開發的最高優先工程基線。每次工作開始前，必須閱讀本文件，以及 docs/ARCHITECTURE.md、docs/HOT_PATH_REVIEW.md、docs/FUNCTION_TRACE.md、docs/CONNECTION_OWNERSHIP.md。

## 1. 文件修訂與整合規則

文件修改必須採「保留後整合」原則：先保留所有仍有效的規範、證據、限制、來源與決策，再做增補或受控整併。

每次修訂工作準則、程式碼或文件時，都必須先進行「精簡與整合檢查」：辨識重複、冗餘、過時、被更高優先規格取代或可由同一權威來源統一表述的內容，能安全合併就合併、能安全刪除就刪除；不得為了縮短文字犧牲有效規範、證據鏈、限制、可追溯性或實作語意。刪除或合併重要內容時，必須在 commit message（提交訊息）或受影響文件中保留可追溯理由。

任何新增或整併的工作規則若產生邏輯衝突，必須先依既有規則的優先順序、適用範圍與目的，嘗試合併為一條不矛盾且最合理的工作規則；若無法在不造成歧義、互斥或破壞既有高優先要求的情況下解決，則不得強行修改、取代或選擇任一衝突版本，並應向使用者詳細說明衝突雙方、已嘗試的整併方式、未能解決的原因與受影響範圍。

長篇研究只保留一個權威版本；其他文件保留必要結論、限制與連結，不得形成互相衝突的第二套規則。

新增或修改任何變數、欄位、狀態、指標、counter、pointer／reference、handle、buffer reference 或其他可變資料，都必須在修改前檢查其作用域、所有權、初始化條件、有效生命週期、可觀察／可變範圍、跨執行緒傳遞、失效條件與最終清理責任；並沿所有可能的成功、錯誤、取消、超時與 shutdown 路徑確認不會使用未初始化資料、超出 owner 生命週期、懸空引用、重複釋放、遺漏清理或狀態殘留。若生命週期或作用不清楚，不得先新增變數再靠後續補救，必須先重新設計其 ownership／scope。

任何原始碼檔案新增或引用 C／Java 函式時，必須核對函式的宣告、定義、完整引數型別與順序、回傳／輸出語意，以及所有引用位置與適用作用域；若函式來自第三方或標準 API，還必須以對應版本的正式宣告／原始碼確認實際簽名，不得只依名稱、舊版本或記憶推定。除了型別與簽名，還必須核對前置條件、後置條件、錯誤契約、輸出參數所有權，以及呼叫者與被呼叫者之間的 ownership／lifetime／reentrancy（重入）／blocking（阻塞）假設；任何間接呼叫，包括 function pointer（函式指標）、JNI method ID、Java method reference（方法參考）或 callback（回呼），均適用相同檢查。修改、移除或重新命名函式時，必須搜尋並檢查整個 repository 的引用與上下游契約，而不得只依第一個編譯錯誤修正。

若修改或新增程式碼涉及型態轉換，必須檢查來源值與目標型別的可表示範圍、符號性、位元寬度、對齊、截斷、指標有效性與安全邊界；並檢查轉換後的值是否仍能滿足直接使用它的函式以及其上下游函式的前置條件、錯誤契約與生命週期要求。未能證明安全的轉換不得以 cast（型別轉換）掩蓋。

自動產生碼與產物驗證：若程式碼由產生器、模板或其他自動化流程產生，必須能追溯其輸入、產生器／模板版本與相關設定；產生碼與手寫碼的可修改邊界必須明確。產生器或產物的任何修改仍須依本文件的函式簽名、前置／後置條件、型態轉換、變數生命週期、編譯、測試與安全規則驗證，不得因「自動產生」而降低審查標準。能重現的產生流程應固定必要輸入與版本，避免同一來源在不同工作階段產生未解釋的差異；產生碼若預期不應手工修改，應以可檢查的工程機制限制或偵測漂移。

工作錯誤與規則回饋：若工作過程出現編譯、測試、靜態分析、整合、研究核對、來源引用或其他可重現錯誤，必須先分析實際原因與受影響範圍，判斷是否存在可由工作準則預防的規則缺口；只有在確認現有規則不足時，才向使用者提出新增規則建議，並先檢查該建議與現有規則是否衝突、重複或可整併。規則本身不得因單一偶發錯誤而過度具體化，除非該錯誤揭示可普遍預防的工程風險。

只要工作結果或研究有可能改變任何 Markdown（MD）文件的規範、證據、限制、決策、流程、索引或與實作一致性的描述，就必須先檢查受影響的 MD 文件，並在本文件規範下進行必要的修改／合併；不得因目前看似只是程式碼或研究工作而跳過文件一致性檢查。

若本機執行環境（Codex 測試環境）無法網路連線，必須先嘗試其他可行方法完成工作目標，例如使用已存在的本機來源、已下載的原始碼／依賴、Git metadata、既有測試資產、可用的快取或其他不依賴即時網路的方法；若在合理範圍內仍無法達成，必須直接向使用者說明無法完成的部分與實際限制，不得以推測結果冒充已驗證結果。

工作成果持久化：必須隨時考慮工作可能因流量限制、新對話或目前對話狀態遺失而中斷；凡是尚未落實成程式碼、測試或正式 MD 的重要研究結果、決策、待辦、限制、驗證狀態或中間成果，應以適當且可追溯的形式留存在 repository（例如權威 MD、研究紀錄、測試資產、程式碼、commit history（提交歷史）或其他具版本控制的工程產物），避免只存在當前對話記憶中。

預設後續 Codex 工作可能在不了解工作現況的情況下開始，因此任何新的工作階段都必須以 repository 中已持久化的規則、文件、程式碼、測試、commit 與研究紀錄為主要現況來源；不得假設新工作階段會自動知道上一個對話的未持久化內容。`docs/WORK_STATE.md` 用於保存跨對話的重要工程現況，但不得取代各專題的權威文件。

每次整併、修改 MD 或修改程式碼都可能造成衝突（conflict／競合）或基於過期內容覆蓋較新成果；因此在每次寫入前，必須重新取得目標檔案的最新內容與版本識別，檢查同一路徑及其相關文件是否已被其他變更更新，並在寫入後檢查 diff／commit 結果與相關文件一致性。若發現版本不一致、競合、未知變更或無法確認寫入基礎，不得直接覆蓋，必須重新同步後再整併。對多檔案相關變更亦必須檢查其彼此引用、規則、索引、ABI 與實作描述是否衝突。

修訂後必須重新檢查 README、架構、hot path（熱路徑）、JNI、lifecycle（生命週期）、測試與安全文件的一致性，並再次確認本次精簡沒有刪掉仍有效的規範、證據或限制。

## 2. 專案與規格

Ckarta 是以 Jakarta Servlet 6.1 為相容性目標的 Servlet container（伺服端小程式容器）與 Web server（網頁伺服器）。Servlet 6.1 的平台要求以正式規格為準；目前 JVM/JNI 研究暫以 OpenJDK 21 為基線。

規格優先順序：RFC／正式標準 → Jakarta Servlet 6.1 → 官方 API specification（應用程式介面規格）→ Ckarta 安全與生命週期不變條件 → Nginx/Tomcat 官方文件與原始碼 → 可驗證同儕審查學術來源 → 其他可靠資料。

## 3. C/Java 邊界

C 負責 socket（通訊端）、event loop（事件迴圈）、I/O multiplexing（輸入輸出多路複用）、HTTP parsing（HTTP 解析）、TLS termination（TLS 終止）、connection management（連線管理）、static file serving（靜態檔案傳送）、reverse proxy（反向代理）、load balancing（負載平衡）、buffering（緩衝）、compression（壓縮）、rate／connection limiting（速率／連線限制）、network access control（網路存取控制）、logging（日誌）與 metrics transport（指標傳輸）。

Java 負責 Servlet API、ServletContext、Request／Response 語意、Filter（過濾器）、Listener（監聽器）、RequestDispatcher（請求分派器）、AsyncContext（非同步內容）、Session（會話）、Web application lifecycle（網頁應用程式生命週期）、class loading（類別載入）、deployment（部署）與 application execution（應用程式執行）。

C 不得直接執行 Servlet application code（Servlet 應用程式程式碼）；Servlet application code 不得在 C event-loop thread 上執行。

## 4. 程序入口

正式產品程序唯一外部入口為 C main()。Java main() 僅可用於測試或工具。

C main() 擁有程序級啟動／停止、原生設定、原生資源、listener／socket、C worker 與 JVM bootstrap coordination（JVM 啟動協調）。JVM 由 C 透過 JNI Invocation API（JNI 虛擬機器啟動介面）建立。

第一階段禁止啟動 JVM 後 fork 並讓子程序繼承已建立 JVM。

完整狀態模型見 docs/ENTRYPOINT_DESIGN.md 與 docs/STARTUP_STATE_MACHINE.md。

## 5. JNI

JNI（Java Native Interface，Java 原生介面）是主要進程內整合方式。JNI crossing（JNI 邊界穿越）必須粗粒度化。

Native buffer（原生緩衝區）若由 Java 透過 DirectByteBuffer（直接位元組緩衝區）觀察，C 必須保證完整生命週期。

JNIEnv pointer（JNI 環境指標）不得跨執行緒共享；native thread 必須依 JNI 規則管理 attachment（附加）與 detach（脫離）。

## 6. JNI 物件化限制

禁止將 C request struct（請求結構）逐欄映射成大量 Java fields、Strings 或 header objects。

目前核准的初步模型：

C canonical request（權威原生請求） → opaque request handle（不透明請求控制代碼） → 一個 Java request facade（請求外觀） → 批次初始化 → DirectByteBuffer data view。

Call*MethodA/V 僅作粗粒度 dispatch（分派）；NewObjectA/V 僅用於必要薄 facade；String／Array 物件化優先延遲；GetPrimitiveArrayCritical 不得作一般零拷貝策略。

詳細成本研究見 docs/JNI_COST_MODEL.md。不得把舊 JNI benchmark（效能基準測試）數字直接套用 OpenJDK 21。

## 7. C 格式與命名

C 沿用 Nginx 命名精神，將 ngx_ 改為 ck_；這不是 Nginx API。縮排只能使用 Tab；大括弧採 Allman style（Allman 風格）。

Java 遵循 Oracle Java Code Conventions：Class UpperCamelCase、method／variable lowerCamelCase、constant UPPER_CASE_WITH_UNDERSCORES。

程式碼包裝與抽象層：除非相容性、安全、生命週期、可測試性、可觀測性、隔離或其他既定工作守則／專案特殊工程要求需要，程式碼的 wrapper／adapter／facade／abstraction（包裝／轉接器／外觀／抽象層）應盡可能少，並貼近實際實作輪廓；不得為追求抽象形式而增加純轉發層。此規則只約束不必要的包裝數量與距離，不取代其他關於實作清晰度、正確性、安全性、可維護性與精簡度的規則。

## 8. 並行與非阻塞

C 採 worker + event loop；Java Servlet 採 executor／thread pool（執行器／執行緒池）。優先 worker ownership（工作者所有權）、sharding（分片）與 immutable state（不可變狀態），不預設 lock-free data structure（無鎖資料結構）。

C event loop 不得執行未知時間的 blocking operation（阻塞操作）。

## 9. HTTP 與連線

HTTP/1.1 framing（訊息框架）必須只有一套規範化解析語意。Content-Length、Transfer-Encoding、chunked encoding、重複標頭與異常訊息框架依適用 RFC 處理。

HTTP request smuggling（HTTP 請求走私）是阻斷式安全需求。

每個 connection 必須有顯式 state machine（狀態機），並具備有界 timeout（逾時）與資源限制。

## 10. 記憶體與零拷貝

request-scoped temporary data（請求範圍暫態資料）優先使用 C memory pool（記憶體池）；pool 不管理 Java heap object（Java 堆積物件）。

zero-copy（零拷貝）是條件式最佳化；TLS、compression 或 Java 內容產生可能需要 CPU processing（CPU 處理）。

## 11. 靜態、代理、Session

靜態檔案預設不進 JVM，必須防 path traversal（路徑穿越）、symlink escape（符號連結逃逸）與 canonicalization mismatch（正規化不一致）。

代理至少規劃 weighted round robin（加權輪詢）、failure counting（失敗計數）、timeout、connection limit、backup server（備援伺服器）與 upstream connection reuse（上游連線重用）。不得未分析方法語意就重試非冪等請求。

Session 語意由 Java Servlet container 管理；C 不建立第二套 Session authority（權威來源）。

## 12. 安全

至少涵蓋 TLS、HTTP security headers（HTTP 安全標頭）、request size limits（請求大小限制）、rate／connection limits、timeouts、access control（存取控制）、request smuggling、Slowloris（慢速攻擊）、buffer overflow（緩衝區溢位）、integer overflow（整數溢位）、use-after-free（釋放後使用）、double free（二次釋放）與 least privilege（最小權限）。

所有外部長度必須檢查；parser 優先 pointer + length（指標加實際長度）。禁止 gets、strcpy、strcat 與無界 sprintf 類用法。

## 13. 測試、相容性與效能

核心模組至少規劃 unit、integration、negative、stress、fuzz、shutdown、resource exhaustion tests（測試）。JNI 必須測 lifetime、重入、例外、取消與 buffer ownership。

必須執行 Jakarta Servlet 6.1 TCK（Technology Compatibility Kit，技術相容性套件）；未通過前不得標示相容。

faster、lower latency、less memory、higher throughput 等宣稱必須有可重現 benchmark，並記錄硬體、OS、kernel、compiler、JDK、TLS、concurrency、request／response size、keep-alive、cache state、版本與 commit。

## 14. 第三方來源

Nginx 與 Apache Tomcat 以 Git submodule（Git 子模組）固定於 third_party/nginx 與 third_party/tomcat；目前版本：Nginx 1.30.4 commit 017cf98dcce217946572a896f0992370475e189f；Tomcat 11.0.25 commit cbe6e15ee81e2fc6232954292a80cca5d1e84009。

禁止未經架構決策直接複製 upstream code（上游程式碼）。移植前必須檢查 license、dependency、平台假設、安全與語意差異。

## 15. 學術與證據規則

任何學術來源必須確認作者、標題、出版資訊、DOI 或穩定網址與可閱讀位置；無法確認就標記「無法確認」且不得引用。

重大決策必須交叉比對固定版本的 Nginx、Tomcat、OpenJDK 與相關學術來源，不得依單一來源作結論。

## 16. 文件索引

docs/ARCHITECTURE.md
docs/HOT_PATH_REVIEW.md
docs/FUNCTION_TRACE.md
docs/CONNECTION_OWNERSHIP.md
docs/DESIGN_DECISIONS.md
docs/ENTRYPOINT_DESIGN.md
docs/STARTUP_STATE_MACHINE.md
docs/HTTP_FRAMING_POLICY.md
docs/CONCURRENCY_MODEL.md
docs/CANCELLATION_MODEL.md
docs/JNI_ABI.md
docs/JNI_COST_MODEL.md
docs/SERVLET_6_1_CRITIQUE.md
docs/WEB_SERVER_THEORY_SERVLET_NGINX.md
docs/OPENJDK_21U_SOURCE_AUDIT.md
docs/THREAD_MODEL.md
docs/THREAD_BENCHMARK_PLAN.md
docs/TCK_INTEGRATION_PLAN.md
docs/SECURITY_BASELINE.md
docs/REFERENCE_SOURCES.md
docs/WORKING_TREE.md
docs/WORK_STATE.md

本文件是工程入口；長篇研究以 docs 對應文件為權威內容。
