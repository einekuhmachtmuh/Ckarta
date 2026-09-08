# Ckarta

Ckarta 是以 Jakarta Servlet 6.1 為相容性目標的 Servlet container（伺服端小程式容器）與高效能 Web server（網頁伺服器）。

專案的核心架構是：

C 資料平面
+
JVM／Java Servlet 容器
+
嚴格定義的 JNI（Java 原生介面）邊界。

## 文件

- WORKING_RULES.md：工程、命名、驗證、安全與自動化基準。
- ARCHITECTURE.md：C/Java 邊界、資料流、並行模型、C 化決策與補充需求。
- HOT_PATH_REVIEW.md：Nginx／Tomcat 實際 hot path（熱路徑）與 whole path（完整路徑）基線。
- SECURITY_BASELINE.md：安全模型與驗證門檻。

## 架構原則

C 主要負責：

- non-blocking I/O（非阻塞輸入輸出）
- event loop（事件迴圈）
- HTTP parsing（HTTP 解析）
- TLS
- static files
- reverse proxy
- load balancing
- rate/connection limiting
- output pipeline

Java 主要負責：

- Jakarta Servlet 6.1
- Servlet lifecycle
- Filter
- Listener
- Session
- ServletContext
- RequestDispatcher
- AsyncContext
- Web application lifecycle
- class loading

## 規格與參考來源

Jakarta Servlet 6.1：
https://jakarta.ee/specifications/servlet/6.1/

Nginx development guide：
https://nginx.org/en/docs/dev/development_guide.html

Apache Nginx source：
https://github.com/nginx/nginx

Apache Tomcat source：
https://github.com/apache/tomcat

RFC 9112：
https://www.rfc-editor.org/rfc/rfc9112.html

SEDA：
https://doi.org/10.1145/502059.502057

## 重要聲明

目前這些文件是架構與工程基線，不代表 Ckarta 已經完成 Servlet 6.1 相容性、Nginx 級安全性或任何效能目標。

所有「已實作」「已通過」「更快」「更安全」的未來宣稱，都必須有 repository 內測試或可重現測量證據。
