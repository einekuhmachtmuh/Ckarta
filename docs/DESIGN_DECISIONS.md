# Ckarta 設計決策矩陣

本文件的完整 JNI 成本研究已集中於 docs/JNI_COST_MODEL.md；thread model 的完整研究已集中於 docs/THREAD_MODEL.md；gateway／Servlet／HotSpot 邊界研究已集中於 docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md。本文件只保留必要決策摘要，以避免重複與失配。

## 1. 事件驅動核心

決策：C 資料平面採 worker + event loop；Linux 優先 epoll。

Nginx：third_party/nginx/src/event/ngx_event.c 的 ngx_process_events_and_timers()；third_party/nginx/src/event/modules/ngx_epoll_module.c 的 ngx_epoll_process_events() 與 epoll_wait()。

Tomcat：third_party/tomcat/java/org/apache/tomcat/util/net/NioEndpoint.java 的 Poller.run()、Poller.processKey()。

## 2. HTTP parser

HTTP/1.1 parser（解析器）留 C，建立單一 framing interpretation（訊息框架解讀）。Nginx 對照為 ngx_http_process_request_line／headers／process_request；Tomcat 對照為 Http11Processor.service／parseRequestLine／parseHeaders／prepareRequest。規格：https://www.rfc-editor.org/rfc/rfc9112.html

## 3. Servlet execution

Java executor／thread pool（執行器／執行緒池）執行 Servlet application；C event-loop thread 不得直接執行 Servlet application code。Tomcat 路徑：SocketProcessor.doRun → Http11Processor.service → CoyoteAdapter.service → Container Pipeline → StandardWrapperValve.invoke → Filter Chain → Servlet.service。

Servlet 6.1 的 AsyncContext 表示 request lifecycle 可超出一次同步 service invocation；Ckarta 必須因此將 connection lifetime 與 Servlet execution lifetime 分離。

## 4. Worker ownership

優先 worker ownership（工作者所有權）與 sharding（分片），不預設 lock-free。Zeldovich 等的 USENIX ATC 2003 研究支持以不共享可變狀態降低事件平行化的同步負擔：https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

## 5. Thread model

第一階段維持三類可實測候選：C worker attached submission、worker-group JNI bridge、central JNI bridge pool。attached worker 只允許作 JNI control／submission，不得執行 Servlet application。

不能在沒有相同 workload benchmark 前宣稱任何一者較快。完整 thread role、shutdown、queue、direct-attach 與 benchmark 設計見 docs/THREAD_MODEL.md。

## 6. JNI 成本決策

OpenJDK 21 Update `jdk-21.0.8-ga` 是 JNI 研究基線。

核心決策：禁止 C request struct（請求結構）逐欄映射成大量 Java field、String 或 header object。

初步採用：

C canonical request
→ opaque request handle（不透明請求控制代碼）
→ 一個 Java request facade（請求外觀）
→ 批次初始化
→ DirectByteBuffer data view（直接位元組緩衝區資料視圖）

理由：OpenJDK 21 HotSpot 的 JNI method invocation 不是單純 C function-pointer jump，而是進入 JNI／HotSpot runtime machinery，再進入 Java call machinery；`NewObjectA/V` 亦涉及 Java instance allocation、JNI handle 與 constructor invocation。DirectByteBuffer 可提供 native memory view，但不負責 Ckarta 的 ownership／lifetime。

完整研究：docs/JNI_COST_MODEL.md、docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/runtime/javaCalls.cpp
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

## 7. Gateway／CGI／Servlet 邊界

Classic CGI：OS process-per-request 類型的 execution boundary；Tomcat `CGIServlet` 同樣透過 `Runtime.exec()` 進入 JDK native process creation path。

Nginx：主要以 FastCGI 等 upstream protocol 將 request 傳至外部 application server；它負責 gateway、buffering、timeout、upstream connection management 等，而不是把 generic CGI execution 塞進 worker。

Tomcat Servlet：application execution 留在 JVM Java thread／executor model。

Ckarta JNI：C data plane 與 Java Servlet execution 同處一 JVM，主要成本轉化為 JNI crossing、HotSpot call machinery、object/data conversion 與 thread scheduling。

這使 Ckarta 不應為同程序 JNI 再引入一個模仿 FastCGI 的 socket/protocol serialization layer，除非該層有實際 isolation、compatibility 或其他工程責任。

完整研究：docs/GATEWAY_SERVLET_NATIVE_BRIDGE_RESEARCH.md

## 8. JNI API 選擇

| API | 初步決策 |
|---|---|
| Call<Type>MethodA/V | 粗粒度 dispatch |
| NewObjectA/V | 僅薄 facade |
| Set/Get field | 避免大量使用 |
| String access | lazy materialization（延遲物件化） |
| Primitive Array | 條件使用，不假設無 copy |
| PrimitiveArrayCritical | 只作受限短操作 |
| NewDirectByteBuffer | 優先作 bulk native data view（大量原生資料視圖） |
| GetDirectBufferAddress | 優先 |

## 9. Session 與 lifetime

Session semantics（會話語意）留 Java。connection lifetime 與 Servlet request lifetime 必須可分離；Tomcat CoyoteAdapter.asyncDispatch() 是重要交叉依據。

## 10. Timer

第一版採 timer tree（計時器樹）；未經 benchmark 不換 timing wheel（時間輪）。Varghese／Lauck DOI：https://doi.org/10.1109/90.650142

## 11. 研究來源限制

Kurzyniec／Sunderam JNI benchmark 與 Bubak 等人的 Janet 研究均早於 OpenJDK 21，不直接提供 Ckarta 的現代 ns／µs 成本數字。

CGI／FastCGI／Servlet 比較研究同樣屬歷史平台實驗；可用來支持「邊界與 workload 會影響結果」的研究方法，不可直接推導 OpenJDK 21 數字。

Janet DOI：https://doi.org/10.1155/2001/582127

SEDA 期刊版 DOI：https://doi.org/10.1145/502059.502057

Capriccio proceedings DOI：https://doi.org/10.1145/945445.945471

Kothari／Claypool：https://web.cs.wpi.edu/~claypool/papers/cgi-perf/
Apte／Hansen／Reeser：https://doi.org/10.1016/S0140-3664(02)00221-9

## 12. 尚未決定

TLS library、allocator strategy beyond pool、HTTP/2、HTTP/3、exact JNI ABI、Java package layout、build system、JNI benchmark threshold、C worker／JNI bridge 的最終 thread count 與 topology。

任何效能優勢宣稱都必須由 Ckarta + OpenJDK 21 可重現 benchmark 證明。

## 13. Web server 理論修正

結論：不推翻 Ckarta 的 C data plane + Java Servlet container；改為「C event-driven data plane + bounded semantic handoff + Java Servlet semantic plane」。理論依據與 Little／SEDA／事件遞送分析見 docs/WEB_SERVER_THEORY_SERVLET_NGINX.md。


## 14. Servlet 6.1 compatibility boundary

決策：保持 Servlet 6.1 compatibility surface（相容性表面），不把 Servlet 6.1 object model（物件模型）當成整個 Web server internal representation（內部表示）。

Level 1 API、Level 2 semantics、Level 3 timing/concurrency semantics 必須遵守；Level 4 internal implementation 保留自由度，允許 C event loop、native buffers、lazy materialization 與不同 executor topology。

完整研究：docs/SERVLET_6_1_CRITIQUE.md

## 15. CGI／FastCGI gateway

決策：CGI/1.1 可作 optional compatibility gateway；FastCGI 優先作 PHP integration。CGI child process 與 Servlet container 不共用 application semantics，也不得讓 child process execution 形成 C event-loop blocking point。完整證據見 docs/CGI_FASTCGI_RESEARCH.md。
