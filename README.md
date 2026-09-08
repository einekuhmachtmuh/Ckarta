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
├── third_party/
│   ├── nginx/        # Git submodule，固定 upstream commit
│   └── tomcat/       # Git submodule，固定 upstream commit
├── c/
├── java/
├── tests/
├── bench/
└── tools/
```

Nginx 與 Apache Tomcat 不直接複製進 Ckarta repository，而是以 Git submodule 固定 upstream commit。

## 文件

- WORKING_RULES.md：工程、命名、驗證、安全、工作成果持久化、自我衝突處理、規則衝突處理與自動化基準。
- docs/ARCHITECTURE.md：C/Java 邊界、資料流、並行模型、C 化決策與補充需求。
- docs/HOT_PATH_REVIEW.md：Nginx／Tomcat hot path（熱路徑）與 whole path（完整路徑）基線。
- docs/FUNCTION_TRACE.md：固定版本的逐函式 hot path 追蹤。
- docs/CONNECTION_OWNERSHIP.md：C 連線、Request、AsyncContext 與 JNI 所有權基線。
- docs/DESIGN_DECISIONS.md：架構決策與證據矩陣。
- docs/ENTRYPOINT_DESIGN.md：C main 入口與 JVM 啟動模型。
- docs/STARTUP_STATE_MACHINE.md：C main、JVM、Java container、network runtime 的啟動／停止狀態機。
- docs/HTTP_FRAMING_POLICY.md：HTTP/1.1 framing（訊息框架）權威解析政策。
- docs/CONCURRENCY_MODEL.md：C 事件並行與 Java Servlet 執行模型。
- docs/THREAD_MODEL.md：C worker、JVM bootstrap、JNI bridge 與 direct-attach 候選的 thread model（執行緒模型）研究基線。
- docs/THREAD_BENCHMARK_PLAN.md：direct attach／JNI bridge／bridge pool 的可重現比較計畫。
- docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md：CGI／FastCGI／Tomcat Servlet／CGIServlet／OpenJDK HotSpot／Ckarta JNI 邊界研究。
- docs/OPENJDK_21U_SOURCE_AUDIT.md：固定 JDK 21u tags 的 HotSpot source audit，核對 21.0.8 與 21.0.11 對 JNI 研究結論的實際影響。
- docs/SERVLET_6_1_CRITIQUE.md：Servlet 6.1、Nginx、Tomcat 與 Web server 理論的中立技術批判及 Ckarta 相容性策略。
- docs/WEB_SERVER_THEORY_SERVLET_NGINX.md：Jakarta Servlet 6.1、Nginx、Tomcat 與 Web server 排隊／並行理論的架構比較。
- docs/CANCELLATION_MODEL.md：連線、Servlet 非同步與 JNI 取消語意。
- docs/JNI_ABI.md：JNI 邊界與所有權門檻。
- docs/JNI_COST_MODEL.md：OpenJDK 21 JNI 跨語言成本模型與 C struct → Java object 策略。
- docs/SECURITY_BASELINE.md：安全模型與驗證門檻。
- docs/TCK_INTEGRATION_PLAN.md：Jakarta Servlet 6.1 TCK 驗證計畫。
- docs/REFERENCE_SOURCES.md：參考原始碼版本、commit、授權與研究規則。
- docs/WORKING_TREE.md：實際 repository 工作樹規劃。
- docs/WORK_STATE.md：跨對話可接手的工程現況、已驗證事項與下一個工程閘門。

`bench/jni/` 是獨立 JNI/thread microbenchmark（微基準測試）資產，不代表正式 Ckarta runtime 已實作。

## 參考原始碼版本

Nginx：stable 1.30.4，commit `017cf98dcce217946572a896f0992370475e189f`。

Apache Tomcat：11.0.25，commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。

OpenJDK 21 JNI 研究基線：`jdk-21.0.8-ga`；另以 `jdk-21.0.11-ga` 做 fixed-tag implementation audit。完整結果見 `docs/OPENJDK_21U_SOURCE_AUDIT.md`。

## 入口與 thread 原則

正式 Ckarta server 入口為 C `main()`；由受控的專用 bootstrap thread（啟動執行緒）透過 JNI Invocation API 建立 JVM，而不是直接在 primordial process thread（原始程序執行緒）上載入 JVM。

第一階段 thread topology 維持可實測候選：C worker attached submission、worker-group JNI bridge、central JNI bridge pool；attached worker 僅能做 JNI control／submission，不得執行 Servlet application。不能在沒有相同 workload benchmark 前宣稱其中任何一者較快。完整理由與研究見 `docs/THREAD_MODEL.md` 與 `docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md`。

JNI request hot path 不採 C struct 逐欄映射為 Java object。Servlet 6.1 的 application semantics 保持，但不作為 C data plane 的內部表示。初步採：

C canonical request
→ opaque request handle
→ 一個 Java request facade
→ 批次初始化
→ DirectByteBuffer data view

OpenJDK 21 的 JNI 成本研究見 docs/JNI_COST_MODEL.md；固定 21u HotSpot implementation audit 見 docs/OPENJDK_21U_SOURCE_AUDIT.md。任何效能結論仍須由可重現 benchmark 證明。

## 架構原則

C：non-blocking I/O（非阻塞輸入輸出）、event loop（事件迴圈）、HTTP parsing（HTTP 解析）、TLS、static file、reverse proxy、load balancing、rate／connection limiting、output pipeline。

Java：Jakarta Servlet 6.1、Servlet lifecycle、Filter、Listener、Session、ServletContext、RequestDispatcher、AsyncContext、web application lifecycle、class loading。

## 重要聲明

目前文件是架構與驗證基線，不代表 Ckarta 已完成 Servlet 6.1 相容性、已通過 TCK、已達到 Nginx 安全程度或已證明效能優越。

所有「已實作」「已通過」「更快」「更安全」宣稱，都必須有 repository 測試或可重現測量證據。