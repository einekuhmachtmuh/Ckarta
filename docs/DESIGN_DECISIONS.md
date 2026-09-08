# Ckarta 設計決策矩陣

本文件的完整 JNI 成本研究已集中於 docs/JNI_COST_MODEL.md；thread model 的完整研究已集中於 docs/THREAD_MODEL.md。本文件只保留必要決策摘要，以避免重複與失配。

## 1. 事件驅動核心

決策：C 資料平面採 worker + event loop；Linux 優先 epoll。

Nginx：third_party/nginx/src/event/ngx_event.c 的 ngx_process_events_and_timers()；third_party/nginx/src/event/modules/ngx_epoll_module.c 的 ngx_epoll_process_events() 與 epoll_wait()。

Tomcat：third_party/tomcat/java/org/apache/tomcat/util/net/NioEndpoint.java 的 Poller.run()、Poller.processKey()。

## 2. HTTP parser

HTTP/1.1 parser（解析器）留 C，建立單一 framing interpretation（訊息框架解讀）。Nginx 對照為 ngx_http_process_request_line／headers／process_request；Tomcat 對照為 Http11Processor.service／parseRequestLine／parseHeaders／prepareRequest。規格：https://www.rfc-editor.org/rfc/rfc9112.html

## 3. Servlet execution

Java executor／thread pool（執行器／執行緒池）執行 Servlet application。Tomcat 路徑：SocketProcessor.doRun → Http11Processor.service → CoyoteAdapter.service → Container Pipeline → StandardWrapperValve.invoke → Filter Chain → Servlet.service。

## 4. Worker ownership

優先 worker ownership（工作者所有權）與 sharding（分片），不預設 lock-free。Zeldovich 等的 USENIX ATC 2003 研究支持以不共享可變狀態降低事件平行化的同步負擔：https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

## 5. Thread model

第一階段採：

C main／control thread
→ JVM bootstrap thread
→ C worker threads
→ JNI bridge thread／pool
→ Java Servlet executor threads

第一階段暫不把所有 C workers 永久 attach JVM。JNI bridge 使用 bounded queue 接收 C worker 的 request descriptor，執行粗粒度 JNI dispatch，再以 completion record 回到原 owner worker。

完整 thread role、shutdown、queue、direct-attach alternative 與 benchmark 設計見 docs/THREAD_MODEL.md。

## 6. JNI 成本決策

OpenJDK 21 Update `jdk-21.0.8-ga` 是 JNI 研究基線。

核心決策：禁止 C request struct（請求結構）逐欄映射成大量 Java field、String 或 header object。

初步採用：

C canonical request
→ opaque request handle（不透明請求控制代碼）
→ 一個 Java request facade（請求外觀）
→ 批次初始化
→ DirectByteBuffer data view（直接位元組緩衝區資料視圖）

理由：OpenJDK 21 HotSpot 的 NewObjectA／NewObjectV 涉及 instance allocation、local JNI handle、argument preparation 與 constructor invocation；GetObjectField 也涉及 object／field 存取與 reference handling。DirectByteBuffer 可提供 native memory view，但不負責 Ckarta 的 ownership／lifetime。

完整研究：docs/JNI_COST_MODEL.md

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

## 7. JNI API 選擇

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

## 8. Session 與 lifetime

Session semantics（會話語意）留 Java。connection lifetime 與 Servlet request lifetime 必須可分離；Tomcat CoyoteAdapter.asyncDispatch() 是重要交叉依據。

## 9. Timer

第一版採 timer tree（計時器樹）；未經 benchmark 不換 timing wheel（時間輪）。Varghese／Lauck DOI：https://doi.org/10.1109/90.650142

## 10. 研究來源限制

Kurzyniec／Sunderam JNI benchmark 與 Bubak 等人的 Janet 研究均早於 OpenJDK 21，不直接提供 Ckarta 的現代 ns／µs 成本數字。

Janet DOI：https://doi.org/10.1155/2001/582127

SEDA 期刊版 DOI：https://doi.org/10.1145/502059.502057

Capriccio proceedings DOI：https://doi.org/10.1145/945445.945471

## 11. 尚未決定

TLS library、allocator strategy beyond pool、HTTP/2、HTTP/3、exact JNI ABI、Java package layout、build system、JNI benchmark threshold、C worker／JNI bridge 的最終 thread count。

任何效能優勢宣稱都必須由 Ckarta + OpenJDK 21 可重現 benchmark 證明。
