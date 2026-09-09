# Ckarta 安全基線

## 1. 安全目標

Ckarta 的安全目標不是「參考 Nginx 與 Tomcat，所以一定同樣安全」。

目標是建立可測試、可審計、可拒絕不安全輸入的安全邊界。

## 2. 威脅模型

至少考慮：

- 惡意 HTTP client（客戶端）
- 惡意或失陷 upstream（上游）
- 超大標頭
- 超大本文
- 慢速傳輸
- 連線耗盡
- parser differential（解析器差異）
- request smuggling
- path traversal
- TLS 資源耗盡
- C memory corruption（C 記憶體毀損）
- Java application exception（Java 應用程式例外）
- upstream timeout
- queue exhaustion（佇列耗盡）
- privilege escalation（權限提升）

## 3. Request smuggling

HTTP request framing 必須只有一個權威語意來源。

特別檢查：

Content-Length
Transfer-Encoding
duplicate Content-Length
invalid chunk syntax
unexpected bytes after message body

代理轉送前應根據既定規則重新建立有效訊息框架。

RFC 9112：
https://www.rfc-editor.org/rfc/rfc9112.html

## 4. Slowloris

必須設定有界：

header timeout
body timeout
keep-alive timeout
TLS handshake timeout

此外應考慮：

per-connection buffer cap（每連線緩衝上限）
global connection cap（全域連線上限）
per-client limit（每客戶端限制）

## 5. C parser security

每個 parser 必須：

- 使用輸入長度。
- 檢查每次 pointer advance。
- 檢查整數加法／乘法溢位。
- 不依賴 NUL termination（NUL 結尾）作為邊界。
- 失敗後不可繼續解析未知狀態。

## 6. Native memory

禁止：

- use-after-free
- double free
- buffer overflow
- out-of-bounds read/write（邊界外讀寫）
- integer overflow leading to undersized allocation（整數溢位導致配置不足）

CI 應規劃 AddressSanitizer、UndefinedBehaviorSanitizer 等工具；實際工具版本與建置參數必須在 CI 文件確認後才列為正式基準。

## 7. Static file security

路徑處理必須防止：

- ../ 穿越
- encoded traversal
- symlink escape
- mount boundary escape（適用時）
- canonicalization mismatch

## 8. TLS

TLS private key 必須：

- 最小檔案權限
- 最小程序權限
- 不進入一般日誌
- 不進入 debug dump

需建立：

- protocol test
- certificate test
- handshake failure test
- session resumption test
- invalid input test

Nginx TLS 模組文件：
https://nginx.org/en/docs/http/ngx_http_ssl_module.html

## 9. HTTP security headers

可由 C output filter（輸出過濾器）提供設定化安全標頭。

可考慮：

Strict-Transport-Security
X-Content-Type-Options
Content-Security-Policy
Referrer-Policy

但不能假設所有網站都適合相同政策；設定應由部署者明確控制。

Tomcat HttpHeaderSecurityFilter：
https://tomcat.apache.org/tomcat-11.0-doc/api/org/apache/catalina/filters/HttpHeaderSecurityFilter.html

## 10. Access control

C：

IP/CIDR access control
rate limiting
connection limiting
route policy

Java：

Servlet/application authorization semantics

Nginx access module：
https://nginx.org/en/docs/http/ngx_http_access_module.html

## 11. Least privilege

公開服務不能長期以 root 執行。

master 與 worker 權限應可分離。

管理平面應與公開 HTTP listener 分離。

## 12. Error handling security

例外與錯誤回應本身是 attack surface，必須防止 stack trace/version/path 洩漏、log injection、retry amplification、error-path memory leak、double completion 與 cancellation 後 use-after-free。完整錯誤架構與 disclosure policy 見 `docs/EXCEPTION_HANDLING_RESEARCH.md`。

## 13. Security testing

必須逐步建立：

- parser fuzzing
- request smuggling test corpus
- Slowloris test
- header bomb test
- body bomb test
- path traversal corpus
- TLS malformed input
- connection exhaustion
- worker queue exhaustion
- upstream timeout／disconnect
- JNI lifetime race

## 13. 安全成熟度判定

「網路安全性不得低於 Nginx 與 Tomcat」不能用功能清單直接證明。

Ckarta 只有在以下證據完成後，才能對外宣稱達到相應安全基準：

1. 功能對標。
2. 安全測試通過。
3. fuzzing 結果。
4. memory sanitizer 結果。
5. dependency vulnerability review（相依套件弱點審查）。
6. protocol interoperability（協定互通）。
7. 長時間壓力測試。
8. 針對已知漏洞類型的 regression test（回歸測試）。

未達成前只能稱為「安全設計目標」，不能稱為「已達成」。

## CGI／FastCGI 額外攻擊面

若啟用 CGI／FastCGI，必須額外限制 executable allowlist、child process count、stdin/stdout/stderr buffer、CPU／wall-clock timeout、environment、working directory、client disconnect cancellation 與 child reaping。不得把 HTTP URI 或 query string 直接交給 shell interpretation（Shell 解譯）。完整方案見 docs/CGI_FASTCGI_RESEARCH.md。

## CGI/FastCGI 模組化安全邊界

CGI/FastCGI module 不屬核心安全邊界之外的「免費功能」；啟用後必須額外套用 executable／upstream allowlist、resource limit、timeout、environment、working-directory、client-disconnect cancellation 與 process／socket lifecycle 驗證。核心在未啟用時不得初始化不必要的 CGI execution state。
