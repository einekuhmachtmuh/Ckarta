# Ckarta 實際工作樹基線

## 1. 根目錄

目前根目錄只保留直接影響工程操作的少量檔案：

.
├── WORKING_RULES.md
├── README.md
├── .gitmodules
├── conf/
├── docs/
├── third_party/
│   ├── nginx/
│   └── tomcat/
├── c/
├── java/
├── tests/
├── bench/
└── tools/

長篇架構、研究、比較與安全文件集中在 docs/。

## 2. third_party

third_party/
├── nginx/       # Nginx Git submodule
└── tomcat/      # Apache Tomcat Git submodule

這兩個目錄不是 Ckarta 執行期相依套件。

它們是固定版本的 reference source（參考原始碼），供：

- function-level trace（逐函式追蹤）
- hot path comparison（熱路徑比較）
- allocation analysis（配置分析）
- blocking analysis（阻塞分析）
- security review（安全審查）

使用。

## 3. c/

c/
├── error/
├── config/
├── core/
├── event/
├── http/
├── tls/
├── connection/
├── proxy/
├── static/
├── cache/
├── limit/
├── output/
├── logging/
└── jni/

目前不是所有子模組都已進入 production；但 `error/`、`core/`、`event/`、`connection/`、`http/`、`output/`、`platform/` 與 `jni/` 已包含可執行或可測試的 C implementation slices。其餘 `tls/`、`proxy/`、`static/`、`cache/`、`limit/`、`logging/` 目前主要仍屬規劃／骨架。這裡的「已實作」只表示 repository 中已有對應 source/test，不代表 production completeness 或 Servlet 6.1 compatibility。

主要 current executable slices：

- `error/`：process-local structured error/outcome record。
- `core/`：C `main()` orchestration、JVM bootstrap 與 completion/event-loop smoke integration。
- `event/`：Linux epoll backend、completion notification primitive，以及 io_uring probe。
- `platform/`：目前 Linux/POSIX socket boundary wrapper。
- `connection/`：connection lifecycle、generation-protected registry 與 reader/output lifetime pins。
- `http/`：HTTP parser、Content-Length/chunked framing、connection reader 與 bounded request-body FIFO。
- `output/`：HTTP response state、final-response serialization 與 bounded nonblocking output writer。
- `jni/`：C/Java JNI runtime、request handoff、completion publisher 與 container-internal async bridge。

`jni/` 是 C/Java ABI 邊界的實作模組之一；正式 ABI 的 canonical contract 仍由 `docs/JNI_ABI.md` 定義。

## 4. java/

java/
└── org/
    └── ckarta/
        ├── bootstrap/
        ├── container/
        ├── connector/
        ├── servlet/
        ├── session/
        └── web/

目前已有 `bootstrap/`、`connector/` 與 `servlet/` 的實作切片，包含 JVM runtime bootstrap、`NativeRequest`、AsyncContext semantic core、Servlet 6.1 API binding prototype 與 `ServletInputStream` minimum semantic adapter。`container/`、`session/`、`web/` 仍未形成完整 Servlet container implementation。

目前不把 Tomcat package hierarchy（套件階層）直接複製成 Ckarta namespace（命名空間）。

## 5. tests/

tests/
├── c/
├── java/
├── integration/
├── protocol/
├── security/
├── compatibility/
└── fuzz/

目前已有 C、Java、HTTP、connection、event、output、completion、JNI/async integration 等測試；`security/`、`compatibility/`、`fuzz/` 仍主要是後續完整 coverage 的工作區。

相容性測試與 Ckarta 自有單元／整合測試分開。

## 6. bench/

bench/
├── http/
├── servlet/
├── proxy/
├── static/
└── tls/

目前 benchmark 工作區仍主要保存 harness／placeholder；正式效能結果必須與 Ckarta、Nginx、Tomcat 的精確版本及測試環境綁定。

## 7. tools/

tools/
├── trace/
├── analysis/
└── development/

工具不得成為正式 runtime dependency（執行期相依套件），除非另有架構決策。

## 8. 空目錄

Git 不直接追蹤空目錄。

在真正程式碼進入前，若需要保留工作樹骨架，可使用 .gitkeep。

## 9. 不可混淆的三個來源層

Ckarta 原始碼：
c/
java/

第三方參考原始碼：
third_party/nginx/
third_party/tomcat/

研究文件：
docs/

不得把第三方程式碼、研究結論與 Ckarta 已實作能力混成同一類。

## 10. Thread model 文件位置

docs/THREAD_MODEL.md 是 Ckarta thread model（執行緒模型）的權威研究文件，集中保存：

- C main／control thread
- JVM bootstrap thread
- C worker threads
- JNI bridge thread／pool
- Java Servlet executor threads
- thread ownership 與 JNI attachment
- queue／backpressure
- shutdown／join 約束
- 尚待 benchmark 驗證的 thread 數量方案

## 11. 跨對話工作狀態

docs/WORK_STATE.md 保存重要的跨對話工程現況、已驗證事項、目前決策與下一個工程閘門。它不是專題規格的第二權威來源；新工作階段仍應先閱讀 `WORKING_RULES.md`，再依當前任務讀取相關權威文件與工作現況。

新工作階段不得假設上一個對話中、但未進 repository 的決策或研究結果仍然存在。

## 12. 啟動配置

c/config/ 保存啟動配置 parser（解析器）與其驗證邊界；conf/ 保存預設主設定檔。配置模組不擁有 JVM 或 network runtime；設定快照由 C main 在不可逆 runtime 初始化前建立與驗證。
