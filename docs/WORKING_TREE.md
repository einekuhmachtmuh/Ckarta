# Ckarta 實際工作樹基線

## 1. 根目錄

目前根目錄只保留直接影響工程操作的少量檔案：

.
├── WORKING_RULES.md
├── README.md
├── .gitmodules
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

模組名稱目前是架構規劃，不代表檔案已實作。

jni/ 是唯一允許直接定義 C/Java ABI（應用程式二進位介面）邊界的 C 模組之一。

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

實際 package（套件）名稱可在開始 Java 實作前確認；目前不把 Tomcat package hierarchy（套件階層）直接複製成 Ckarta namespace（命名空間）。

## 5. tests/

tests/
├── c/
├── java/
├── integration/
├── protocol/
├── security/
├── compatibility/
└── fuzz/

相容性測試與 Ckarta 自有單元／整合測試分開。

## 6. bench/

bench/
├── http/
├── servlet/
├── proxy/
├── static/
└── tls/

所有效能結果必須與 Ckarta、Nginx、Tomcat 的精確版本及測試環境綁定。

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
