# Ckarta Gateway／Servlet／Native Bridge 研究

本文件是目前「CGI／FastCGI／Tomcat Servlet／Tomcat CGIServlet／Ckarta JNI」邊界比較的權威研究文件。它用固定版本 upstream source 與 OpenJDK 21 HotSpot source 追蹤實際 native execution path（原生執行路徑），並作為 thread／JNI benchmark 的設計依據；不直接宣稱任何方案較快。

## 1. 固定版本

Nginx 1.30.4：`017cf98dcce217946572a896f0992370475e189f`
https://github.com/nginx/nginx/tree/017cf98dcce217946572a896f0992370475e189f

Tomcat 11.0.25：`cbe6e15ee81e2fc6232954292a80cca5d1e84009`
https://github.com/apache/tomcat/tree/cbe6e15ee81e2fc6232954292a80cca5d1e84009

OpenJDK 21 Update：`jdk-21.0.8-ga`
https://github.com/openjdk/jdk21u/tree/jdk-21.0.8-ga

## 2. Nginx：不是一般 CGI executor

Nginx 官方 HTTP 文件提供 FastCGI、SCGI、uWSGI 等 upstream protocol（上游協定）模組；固定版本 source 有 `ngx_http_fastcgi_module.c`，其功能是把 request 傳給 FastCGI server，而不是把一般 CGI script 直接在 Nginx worker 內執行。

FastCGI 路徑可概括為：

HTTP client
→ Nginx request processing
→ FastCGI parameters／request body
→ upstream FastCGI server
→ response

Nginx 因而主要負責 routing、parameter construction、buffering、timeout、upstream connection management、failure handling；動態程式由 FastCGI application server 執行。

來源：
https://nginx.org/en/docs/http/ngx_http_fastcgi_module.html
https://nginx.org/en/docs/beginners_guide.html
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/modules/ngx_http_fastcgi_module.c

## 3. Classic CGI 與 FastCGI

Classic CGI 的典型邊界是 OS process（作業系統程序）：

server
→ create process
→ CGI environment
→ stdin
→ executable
→ stdout
→ process exit

FastCGI 可讓 application process 長期存在，因此攤銷 process startup cost；但仍有 protocol／serialization、socket、kernel scheduling、buffering 與 upstream failure semantics。

所以 FastCGI 的重要價值不只是「比較快」，而是把動態 execution、failure boundary、resource control 與 frontend request handling 分開。

## 4. Tomcat 標準 Servlet

固定 Tomcat 11.0.25 的典型動態路徑：

NioEndpoint
→ SocketProcessor
→ Http11Processor.service()
→ CoyoteAdapter.service()
→ Container Pipeline
→ StandardWrapperValve
→ Filter Chain
→ Servlet.service()

這條路徑全部位於同一 JVM；沒有 FastCGI 的外部 application-server protocol，也沒有 classic CGI 的每-request OS process boundary。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/coyote/http11/Http11Processor.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/connector/CoyoteAdapter.java

## 5. Tomcat CGIServlet

Tomcat 11.0.25 的 `CGIServlet.CGIRunner.run()` 明確呼叫 `Runtime.getRuntime().exec(...)`，再透過 child process 的 stdin/stdout/stderr 傳遞 request／response；stderr 還使用獨立 thread 讀取。

實際路徑：

Servlet request
→ CGIServlet
→ Runtime.exec
→ JDK ProcessImpl native path
→ OS process creation
→ CGI executable
→ stdio
→ Servlet response

因此其 native execution 主要是 OS process creation／IPC；它不是 C→Java JNI `JavaCalls::call` 型態的 in-process invocation。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/servlets/CGIServlet.java
https://docs.oracle.com/en/java/javase/21/docs/api/java.base/java/lang/Runtime.html
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/java.base/unix/classes/java/lang/ProcessImpl.java

## 6. OpenJDK 21 HotSpot：JNI 真正進入 JVM 後的路徑

固定 OpenJDK 21 Update source 中，JNI method invocation 從 `jni.cpp` 的 native entry 進入 HotSpot runtime；method invocation 接著進入 `JavaCalls::call` 等 machinery。

`JavaCalls::call` 涉及 Java thread／VM state、method metadata、calling convention、compiled/interpreted entry 與 call stub；因此 C→Java JNI method call 不是單純 function-pointer jump。

`NewObjectA/V` 也不是配置一段 C-like memory：object construction 需要 Java instance allocation、JNI reference/handle、constructor invocation 與 Java object semantics。

因此 C request struct 若逐欄建立 Java mirror object，成本來源至少包含 data translation、object allocation、JNI handle 與 constructor／field processing；它不能視為零拷貝記憶體映射。

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/runtime/javaCalls.cpp

## 7. 五種模型的邊界位置

| 模型 | 邊界 | 主要成本 | execution context 重用 | Ckarta 啟示 |
|---|---|---|---|---|
| Classic CGI | OS process | process creation、IPC、environment | 否，典型每 request 建立 | 強隔離但不適合作為主動態路徑 |
| Nginx FastCGI | protocol／socket／upstream | serialization、socket、kernel scheduling、buffering | 是 | gateway、backpressure、timeout 很有價值，但不應照搬 socket protocol |
| Tomcat Servlet | JVM call graph／Java threads | scheduling、object／framework cost | 是 | 最接近 Ckarta 的 Java execution layer |
| Tomcat CGIServlet | JVM → OS process | JVM + process creation + stdio/IPC | CGI 部分否 | 顯示 OS process 是更重的 execution boundary |
| Ckarta JNI | in-process JNI／HotSpot | JNI entry、argument handling、VM call machinery、object/data cost | 是 | 應最小化 crossing 及不必要 objectification |

## 8. 對 Ckarta thread model 的啟示

前一階段 central JNI bridge 的 microbenchmark 已顯示，在極小 JNI workload 中，queue handoff 可能遠大於單次 method invocation。因此 bridge thread/pool 不能先驗視為最佳方案。

正式候選應至少包括：

A. stable C worker 一次 AttachCurrentThread，長期重用自身 `JNIEnv*`。

B. worker group 對應受控 JNI bridge，避免中央 queue 成為單點序列化瓶頸。

C. central bridge pool，只有在實測證明 queue contention、CPU locality、Servlet scheduling 或 lifecycle 管理有收益時採用。

這裡的比較必須使用相同 request workload；不能拿 primitive microbenchmark 直接代表 Servlet request path。

## 9. Wrapper／抽象層的啟示

Ckarta 應把抽象限制在真正承擔 compatibility、security、lifecycle、isolation、testability、ownership 等工程責任的位置。

推薦：

C canonical request
→ stable ABI descriptor
→ necessary opaque handle
→ one Java request facade
→ Servlet API

不推薦：

C struct
→ C adapter
→ JNI DTO
→ Java DTO
→ Java adapter
→ Servlet facade
→ Servlet request

除非其中一層具有明確工程責任。

## 10. 學術研究的限制

Kothari／Claypool 對 CGI、FastCGI、Servlets 的實驗，以及 Apte／Hansen／Reeser 對 CGI/C++、FastCGI/C++、Servlets、JSP 的比較，都指出效能排序會受 workload 與 application complexity 影響。

因此這些研究可支援「邊界成本與 workload 相關」的設計思想，但不應直接移植其舊 JVM 的 ns／µs 或 throughput 數字到 OpenJDK 21。

來源：
https://web.cs.wpi.edu/~claypool/papers/cgi-perf/
https://www.sciencedirect.com/science/article/pii/S0140366402002219

Zeldovich 等人的 event-driven multiprocessor 研究支持 coarse-grained parallelism；Capriccio 則提供 thread-based server 的另一條可擴展路徑。因此 Ckarta 的 thread topology 應由 ownership 與 measurement 決定，而非預設 event 或 thread 必然較優。

來源：
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs
https://dblp.org/rec/conf/sosp/BehrenCZNB03

## 11. 暫定原則

1. 不採 classic CGI 的 OS-process-per-request 作為 Ckarta 動態 Servlet 主路徑。
2. 吸收 FastCGI 的 gateway、bounded resource、timeout、failure handling 思想，但不把其 socket／protocol serialization 複製到同 process JNI 邊界。
3. JNI 保持粗粒度，以 request/stage 為單位；避免逐 header／body chunk crossing。
4. C 保持 request canonical ownership；Java 取得受控 facade 與 native buffer view。
5. thread topology 在正式 benchmark 前維持 A/B/C 候選，不宣稱任何一種先驗最佳。
6. wrapper 只在有實際工程責任時增加；純轉發層應避免。

## 12. 尚待驗證

- long-lived C worker JNI attachment 的 shutdown／failure semantics。
- direct attach、per-worker bridge、central bridge pool 的 queue／tail latency／CPU locality。
- request descriptor + DirectByteBuffer + Java facade 的 allocation／GC。
- JNI scheduling 與 Java executor scheduling 疊加後的 p95/p99。
- JNI exception、client disconnect、AsyncContext、connection close、worker ownership 的交互。
- OpenJDK 21.0.8 與本機 21.0.11 的 runtime 差異；效能數據不得混用。
