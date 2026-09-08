# Ckarta

Ckarta 是以 Jakarta Servlet 6.1 為相容性目標的 Servlet container（伺服端小程式容器）與高效能 Web server（網頁伺服器）。

核心架構：

C 資料平面
+
JVM／Java Servlet 容器
+
嚴格定義的 JNI（Java 原生介面）邊界。

## 工作樹

目前 repository（儲存庫）採以下基準：

```
.
├── WORKING_RULES.md
├── README.md
├── .gitmodules
├── docs/
│   ├── ARCHITECTURE.md
│   ├── HOT_PATH_REVIEW.md
│   ├── SECURITY_BASELINE.md
│   └── REFERENCE_SOURCES.md
├── third_party/
│   ├── nginx/        # Git submodule，固定 upstream commit
│   └── tomcat/       # Git submodule，固定 upstream commit
├── c/
├── java/
├── tests/
├── bench/
└── tools/
```

Nginx 與 Apache Tomcat 不直接複製進 Ckarta repository，而是以 Git submodule 固定 upstream commit。這使參考原始碼可重現、可更新、可與 Ckarta 自身修改清楚區分。

## 文件

- WORKING_RULES.md：工程、命名、驗證、安全與自動化基準。
- docs/ARCHITECTURE.md：C/Java 邊界、資料流、並行模型、C 化決策與補充需求。
- docs/HOT_PATH_REVIEW.md：Nginx／Tomcat hot path（熱路徑）與 whole path（完整路徑）基線。
- docs/SECURITY_BASELINE.md：安全模型與驗證門檻。
- docs/FUNCTION_TRACE.md：固定版本的逐函式 Hot Path（熱路徑）追蹤。
- docs/CONNECTION_OWNERSHIP.md：C 連線、Request（請求）、AsyncContext（非同步內容）與 JNI 所有權基線。
- docs/DESIGN_DECISIONS.md：架構決策與 Nginx/Tomcat／學術證據矩陣。
- docs/WORKING_TREE.md：實際 Repository 工作樹規劃。
- docs/REFERENCE_SOURCES.md：參考原始碼版本、commit、授權與研究使用規則。

## 參考原始碼版本

Nginx：stable 1.30.4，commit `017cf98dcce217946572a896f0992370475e189f`。

Apache Tomcat：11.0.25，commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。

Nginx 官方目前列出的 stable 版本為 1.30.4；Apache Tomcat 目前 11.x 下載頁列為 11.0.25。版本更新時必須重新做 hot path 與安全基線核對。

## 架構原則

C：

- non-blocking I/O（非阻塞輸入輸出）
- event loop（事件迴圈）
- HTTP parsing（HTTP 解析）
- TLS
- static file serving（靜態檔案傳送）
- reverse proxy（反向代理）
- load balancing（負載平衡）
- rate／connection limiting（速率／連線限制）
- output pipeline（輸出管線）

Java：

- Jakarta Servlet 6.1
- Servlet lifecycle（Servlet 生命週期）
- Filter
- Listener
- Session
- ServletContext
- RequestDispatcher
- AsyncContext
- Web application lifecycle（網頁應用程式生命週期）
- class loading（類別載入）

## 規格與參考

Jakarta Servlet 6.1：
https://jakarta.ee/specifications/servlet/6.1/

Nginx：
https://nginx.org/
https://github.com/nginx/nginx

Apache Tomcat：
https://tomcat.apache.org/
https://github.com/apache/tomcat

RFC 9112：
https://www.rfc-editor.org/rfc/rfc9112.html

## 重要聲明

目前文件是架構與工程基線，不代表 Ckarta 已完成 Servlet 6.1 相容性、Nginx 級安全性或任何效能目標。

所有「已實作」「已通過」「更快」「更安全」等宣稱，都必須有 repository 內測試或可重現測量證據。
