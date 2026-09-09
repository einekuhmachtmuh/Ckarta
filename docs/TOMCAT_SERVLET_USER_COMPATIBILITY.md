# Tomcat Servlet 使用者相容性基線

## 目的

本文件整理公開網路上由真人作者撰寫的 Tomcat／Servlet 開發筆記、源碼學習文章與使用心得，再與 Ckarta `main` 目前已實作／已定義的流程比較。它是「使用者心智模型與相容性」文件，不是 Tomcat 私有實作的移植規格，也不授權後續直接照抄 Tomcat internals。

**本輪只把研究與比較結果寫入本文件；不得因本文件本身直接修改 Ckarta 實作。** 後續工程修改仍必須依 `WORKING_RULES.md` 的規格優先順序與正式 Servlet 6.1 API／規格決定。

## 1. 研究材料與證據層級

### 1.1 真人撰寫的 Tomcat／Servlet 開發筆記與心得

1. **Roberto T.／Ben Vedder 等作者維護的《How Tomcat Works》電子書整理版**。該書明確表示目的在於先建立 Catalina 的大圖，再逐步拆解元件；其讀者包括 servlet/JSP programmer 與 Tomcat user。它特別把 servlet lifecycle、container component hierarchy、request processing 等作為理解 Tomcat 的入口。來源：https://l-webx.gitbooks.io/how_tomcat_works/content/

2. **yangykaifa，〈詳細介绍：Tomcat源码分析三(Tomcat请求源码分析)〉，2026-02-14**。作者從實際除錯 Spring MVC、自訂 Filter／Valve 常見問題切入，總結 Connector → CoyoteAdapter → StandardHostValve → StandardContextValve → StandardWrapperValve → ApplicationFilterChain → Servlet，並強調 Request 封裝、Context class-loader binding、Filter chain 與 Wrapper 對 servlet lifecycle 的責任。來源：https://www.cnblogs.com/yangykaifa/p/19616315

3. **959_1x，〈Tomcat 源码分析 (Tomcat请求处理流程) (五)〉，2022-07-31**。作者將 CoyoteAdapter 後的 request flow 分成 Engine、Host、Context、Wrapper，並指出 WrapperValve 負責 Servlet 建立／初始化、FilterChain 建立與最後 `Servlet.service()`。來源：https://blog.51cto.com/c959c/5529851

4. **wx63c373b99113d，〈浅谈Tomcat接收到一个请求后在其内部的执行流程（源码）〉，2023-01-18**。作者明確聲明文章基於 Tomcat 7.x、BIO、HTTP，只討論執行流程；其核心心智模型是「OS buffer → Tomcat I/O → protocol parse → mapping → Pipeline → Servlet → response」。來源：https://blog.51cto.com/u_15942107/6019505

5. **bhupesh／01010011，〈servlet의 동작방식과 thread safety〉，2016-12-29**。作者從 Servlet 的 thread-safety 問題整理出常見使用者心智模型：Servlet、Filter、Listener 同處 application scope；共享成員狀態需要併發安全；請求中的 local state 比 shared member state 更容易安全。來源：https://01010011.blog/2016/12/29/servlet%EC%9D%98-%EB%8F%99%EC%9E%91%EB%B0%A9%EC%8B%9D%EA%B3%BC-thread-safety/

6. **Peter Cipov，〈NO (not only) Servlet〉**。作者從 container implementation 的角度提醒 request／response 物件可能被 recycle／reuse，因此 application code 不應把 request object 當成長期擁有的 immutable object；這是與 request lifetime、thread safety 密切相關的使用者認知。來源：https://www.petercipov.com/posts/not_only_servlet/

7. **Coderanch Tomcat multithread servlet 討論串（Tim Moores 等參與）**。討論釐清「單一 Servlet instance」與「多 thread concurrently 呼叫 service」並不矛盾；Servlet code 必須假設 concurrent invocation。來源：https://coderanch.com/t/570543/application-servers/Tomcat-multithread-servlet

8. **laojean，〈我用Tomcat搭建Servlet应用，收获了哪些实用技巧〉，2026**。作者從實際使用 Filter、Servlet lifecycle、session 等角度總結開發經驗；其中 Filter 被描述成 request gate，`doFilter()` 是否呼叫 `chain.doFilter()` 會決定是否繼續往後執行。來源：https://blog.51cto.com/u_14256/14887374

### 1.2 正式 Tomcat／Servlet 證據

1. Tomcat 11.0.25 Servlet 6.1 API：Servlet lifecycle、multithreaded execution 與 shared state 注意事項。來源：https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/Servlet.html

2. Tomcat 11.0.25 Servlet 6.1 API：`HttpServlet` 的 `service()` 負責依 HTTP method dispatch 到 `doGet()`、`doPost()` 等，並明確提醒 Servlet 在 multithreaded server 中處理 concurrent requests。來源：https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/http/HttpServlet.html

3. Tomcat 11.0.25 Servlet 6.1 API：`ServletContext` 是每一個 web application 在 JVM 中的 context authority；不能把 distributed application 的 context 當成真正全域資料容器。來源：https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/ServletContext.html

4. Tomcat 11.0.25／固定研究 commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009` 的 `CoyoteAdapter`、`StandardWrapper`、`AsyncContextImpl` 等 upstream source：Coyote request 到 Catalina Request/Response 的轉換、container Pipeline 呼叫、request thread 記錄、async lifecycle、recycle 與 error processing。來源：
   - https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/connector/CoyoteAdapter.java
   - https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/StandardWrapper.java
   - https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/AsyncContextImpl.java

## 2. 真人筆記反覆形成的「Tomcat/Servlet 使用者既定心智模型」

### 2.1 Servlet 是 application component，不是 socket handler

真人筆記雖然對 Tomcat 內部層級的描述深淺不同，但高度集中於同一使用者視角：開發者寫的是 Servlet／Filter／Listener／Session 等 Java application components；Connector、Coyote、Pipeline、Valve 與 socket/I/O 是 container 的責任，而不是 Servlet 作者直接管理的責任。

這與正式 Servlet API 的定位一致：Servlet interface 定義的是 container 與 servlet class 之間的 contract；`ServletRequest` 也是由 container 建立後交給 `service()`。來源：https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/Servlet.html 、 https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/ServletRequest.html

**對 Ckarta 的相容性含義：** C 不應把 socket／event-loop API 暴露為 Servlet programming model；Java 應看到 Servlet API 的 request/response/context，而不是 C connection object。

### 2.2 一次 HTTP request 不是直接「呼叫某個 Servlet」

真人 source-reading 文章幾乎都把中間 routing chain 當成理解 Tomcat 的必要部分：Connector/Coyote → Engine → Host → Context → Wrapper → FilterChain → Servlet。這種講法的價值不在於要求新容器複製每一個 Java class，而在於讓使用者知道「URL mapping、virtual host、web application context、servlet mapping、filters」在 Servlet 到達前都有語意。

Ckarta `main` 目前已經有相同的**概念性**分層：C data plane → route → Java semantic handoff → Java Container/Filter/Servlet target state；但實作上尚未完成 Engine/Host/Context/Wrapper 與正式 mapping。

### 2.3 Filter 是可終止的 request chain，不是單純前置 hook

真人筆記最常重複的觀念是：`Filter#doFilter()` 可以選擇呼叫或不呼叫 `chain.doFilter()`；所以 filter 不只是「請求前執行的一段 callback」，而是可以終止、轉交、甚至對 response 做後置處理的 chain node。來源：https://blog.51cto.com/u_15060510/2640926 、 https://blog.51cto.com/u_14256/14887374

**Ckarta 相容性要求：** 未來 Filter implementation 必須保留這個 semantics。C native pipeline 可以做安全檢查，但不能把 Filter 降級成只能在 Servlet 前執行一次的固定 middleware。

### 2.4 Servlet instance 與 request invocation 必須分開理解

真人使用筆記與技術討論反覆指出：預設心智模型不是「每一 request `new Servlet()`」；Servlet instance 生命週期由 container 管理，而不同 request 可以 concurrent invocation 同一 instance。正式 Tomcat 11.0.25 `Servlet` 與 `HttpServlet` API 也明確提醒多請求並行，因此 instance fields／class fields 的 shared mutable state 必須由 application 自己處理同步。來源：https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/Servlet.html 、 https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/http/HttpServlet.html 、 https://coderanch.com/t/570543/application-servers/Tomcat-multithread-servlet

**Ckarta 相容性要求：** Java application 不應察覺到「C 每個 request 有自己的 native worker」就因此推導「Servlet instance 不共享」。request concurrency 與 Servlet instance lifecycle 必須仍由 Java container semantics 定義。

### 2.5 Request／Response 有明確的 container lifetime，不是 application 永久 object

Tomcat source 與使用者心得都指出 request／response 可能 recycle/reuse。Tomcat 11 的 `org.apache.coyote.Request` 也明確提供 `recycle()` 與 request-thread bookkeeping。來源：https://tomcat.apache.org/tomcat-11.0-doc/api/org/apache/coyote/Request.html

**Ckarta 相容性要求：** application 若要把 request-derived data 帶出當前 invocation，必須遵守 Servlet specification 對 async/lifetime 的規則；C native layer 不可以因自己保留 native request descriptor 就默認 Java request object 永遠有效。

### 2.6 AsyncContext 是「request 還活著，但原始 servlet invocation 可以結束」的模型

真人 async Servlet 筆記常將 `startAsync()`、`AsyncContext.start()`、`complete()`、`AsyncListener` 視為完整生命週期，而非單一背景 thread helper。正式 Servlet 6.1 API 的 `AsyncListener` 把 `onComplete`、`onError`、`onTimeout`、`onStartAsync` 分開；Tomcat `AsyncContextImpl` 也維護 started/completion/error/recycle 的內部狀態。

**Ckarta 相容性要求：** C request/connection lifetime 必須能長於一次 Java `service()` call；`service()` 返回不能自動視為 network response 已完成。

### 2.7 Exception / error 與 completion 不是同義詞

真人開發者常從 `ServletException`、`IOException`、Filter exceptions、async errors 等不同層面討論錯誤；Tomcat 也將 asynchronous error processing 與 completion/error listener 分開。

**Ckarta 相容性要求：** 目前 `ck_error`／terminal publication 架構方向正確，但最終應再加上 Servlet-level Throwable / HTTP error response / connection I/O failure 的 mapping，不可只靠 C status code 代表完整 Servlet semantics。

### 2.8 ServletContext／Session／Listener 是 application scope semantics

使用者筆記常把 ServletContext、Session、Listener 視為 application container 提供的 shared semantics，而非單一 request-local object。正式 `ServletContext` API 也把 context 定義為 web application 的 container communication surface。來源：https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/ServletContext.html

**Ckarta 相容性要求：** C 不建立第二套 Session/ServletContext authority；這些必須留在 Java semantic plane。

## 3. 與 Ckarta main 的詳細比較

| 使用者可觀察概念 | Tomcat／Servlet 使用者既定模型 | Ckarta main 目前狀態 | 相容性評價 |
|---|---|---|---|
| 外部入口 | container 接受 network request，再交給 Servlet container | C main 是程序入口，C data plane 預計承擔 network/event loop | **可相容**，只要 Java API 語意保持不變 |
| HTTP parsing | Connector/Coyote 負責 | C planned | **未完成** |
| Request object | container 建立 `HttpServletRequest` | C canonical descriptor + 尚未完成正式 Java request facade | **方向正確，未完成** |
| Engine/Host/Context/Wrapper | 使用者不直接呼叫，但其 mapping semantics 會影響結果 | 只在文件／target architecture，尚未完成正式 container tree | **重大未完成項** |
| FilterChain | matching + ordered chain；Filter 可終止 chain | 文件有 Java Filter chain 目標，尚未正式實作 | **未完成** |
| Servlet instance lifecycle | container 建立／初始化／管理；可被多 request concurrent invoke | Java container 尚未真正完成 | **未完成** |
| Servlet thread safety | application 必須假設 concurrent requests | Java executor 已為 concurrent boundary，但目前 smoke 只有最小 task | **概念相容、語意未完成** |
| ServletContext | web-app scoped container object | 未完成正式 ServletContext implementation | **未完成** |
| Session | Java container authority | C 不建立第二套 Session authority | **架構方向相容** |
| Request recycling | container 可 recycle internal request structures | C 有 explicit native ownership，但 Java request recycle 尚未實作 | **需正式 lifecycle implementation** |
| AsyncContext | async mode 可使 request lifecycle 超出原 `service()` invocation | Ckarta 有 completion/cancellation state prototype，但沒有 AsyncContext bridge | **重大未完成項** |
| AsyncListener | start/error/timeout/complete 是不同事件 | `ck_error`／completion 有基礎，但 listener semantics 未完成 | **未完成** |
| HTTP response | Servlet response object → connector/output | C output pipeline 尚未實作 | **未完成** |
| Exception propagation | Java Throwable / ServletException / IOException / async error 有規定語意 | C structured error 已存在，但 Java Throwable translation 尚簡化 | **部分相容** |
| Completion | request result 不等於 Java method return | native completion protocol 已完成 first executable slice | **架構相容** |
| Cancellation | async/request/connection cancellation semantics 綁定 lifecycle | request cancellation已存在；connection/AsyncContext 尚未接通 | **部分相容** |
| Class loading / deployment | Java container 控制 webapp classloading/deployment | 尚未完成 | **未完成** |

## 4. 最重要的相容性差異

### A. Ckarta 可以改變「誰負責 I/O」，不能改變「Servlet 使用者看到的生命週期」

Tomcat user 不應需要知道 request 由 C event loop 還是 Java NIO 接收；他需要的是 Servlet API 的 request/response/filter/async/session semantics。這是 Ckarta 架構最重要的 compatibility boundary。

### B. Ckarta 的 bounded handoff 是內部實作，不應變成 Servlet-visible queue semantics

目前 native completion queue、Java executor queue、eventfd 都是實作層。Servlet user 不應看到 queue depth、eventfd、owner token 等 native implementation detail。

### C. `service()` 返回不是 network completion

Tomcat/Servlet async programming 已經建立這個觀念；Ckarta 必須保持。這是目前 Ckarta connection ownership／AsyncContext integration 的主要未完成點。

### D. Filter／Valve／C pipeline 三者不得互相冒充

Tomcat users 知道 Filter 是 Servlet API object；Valve 是 Tomcat-specific container interception mechanism；Nginx phase handler 又是另一種 native event/data-plane mechanism。Ckarta 可以有 C middleware/phase、Java Filter，但不能把 C middleware 說成 Servlet Filter，也不能要求 application code 依賴 Tomcat private Valve API。

### E. Thread ownership 必須對使用者透明但 semantics 必須相容

Ckarta 可以有 C event worker + Java executor；但 Servlet code 必須繼續面對 Servlet 規格要求的 concurrent execution、request lifetime、async lifecycle，而不能因 native worker topology 而得到不同語意。

## 5. 對目前 Ckarta 的建議（本文件本輪只作研究，不直接觸發程式修改）

1. **把正式 Servlet container hierarchy 優先於更多 JNI micro-optimization。** 先建立 Context／mapping／Wrapper／FilterChain 的實際 semantics，否則 JNI completion 雖然漂亮，卻沒有真正的 Servlet compatibility target。

2. **AsyncContext 應成為 connection ownership 模型的上層 authority。** Native connection 不能因 `service()` 返回就 free；必須等待 Servlet async completion / error / timeout / client disconnect 等事件決定 terminal outcome。

3. **Connection cancellation 與 Servlet async error 必須用 correlation identity 連接，而不是共享 native pointer。** request_id、owner_token、lifetime_token 可以作 native validation，但 Java application 不應看到這些 internal tokens。

4. **FilterChain 要保留短路與 response-after semantics。** 尤其 `chain.doFilter()` 前後都可能有 application-visible 行為；只實作「先執行 filter 再執行 servlet」是不夠的。

5. **Servlet instance concurrency 必須測試。** 至少要有同一 Servlet instance concurrent `service()` invocation、shared instance field、ServletContext／Session shared state 的 compatibility tests。

6. **Request recycling 必須成為顯式測試維度。** application 在 async completion 後仍能否使用 request/response 的規則，必須直接對照 Servlet 6.1 規格及 Tomcat 行為，而不能從 C native lifetime 自行推導。

7. **不要把 Tomcat private class graph 當 Ckarta ABI。** 相容目標應是 Servlet API semantics；Tomcat `Engine/Host/Context/Wrapper/Valve` 主要是理解使用者期待與驗證 reference implementation behavior 的模型。

## 6. 本文件的證據限制

真人 blog／論壇／心得用於回答「Tomcat/Servlet 開發者通常如何理解系統」，不是規範來源；其中部分文章以 Tomcat 7/8/9 或較舊 Java 版本為背景，不能拿舊 implementation detail 直接當 Servlet 6.1 規格。正式相容性判斷以 Servlet 6.1 規格／API 及固定 Tomcat 11.0.25 source 為準。

尤其：

- `SingleThreadModel` 已 deprecated，不應作新相容性模型。
- Tomcat private Valve、Coyote note、request recycling implementation 等不是 Servlet API contract 本身。
- 人類筆記中常見的「單例 servlet」說法應理解成 container instance/lifecycle 的實作與 API 使用心智模型，而不能把它擴張成所有 servlet container 必須採固定單例配置。

## 7. 來源索引

### 真人文章／討論

- How Tomcat Works — https://l-webx.gitbooks.io/how_tomcat_works/content/
- yangykaifa — https://www.cnblogs.com/yangykaifa/p/19616315
- 959_1x — https://blog.51cto.com/c959c/5529851
- wx63c373b99113d — https://blog.51cto.com/u_15942107/6019505
- bhamaoth／01010011 — https://01010011.blog/2016/12/29/servlet%EC%9D%98-%EB%8F%99%EC%9E%91%EB%B0%A9%EC%8B%9D%EA%B3%BC-thread-safety/
- Peter Cipov — https://www.petercipov.com/posts/not_only_servlet/
- Coderanch discussion — https://coderanch.com/t/570543/application-servers/Tomcat-multithread-servlet
- laojean — https://blog.51cto.com/u_14256/14887374

### 正式規格／官方 API

- Servlet 6.1 API — https://tomcat.apache.org/tomcat-11.0-doc/servletapi/
- Servlet 6.1 `Servlet` — https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/Servlet.html
- Servlet 6.1 `HttpServlet` — https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/http/HttpServlet.html
- Servlet 6.1 `ServletContext` — https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/ServletContext.html
- Servlet 6.1 `AsyncListener` — https://tomcat.apache.org/tomcat-11.0-doc/servletapi/jakarta/servlet/AsyncListener.html

### 固定 Tomcat source

- CoyoteAdapter — https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/connector/CoyoteAdapter.java
- StandardWrapper — https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/StandardWrapper.java
- AsyncContextImpl — https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/AsyncContextImpl.java
- Coyote Request API — https://tomcat.apache.org/tomcat-11.0-doc/api/org/apache/coyote/Request.html
