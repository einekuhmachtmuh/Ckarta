# Ckarta 例外處理、錯誤傳播與恢復架構研究

本文件是 Ckarta 例外處理與跨層錯誤傳播的唯一長篇研究權威文件。這裡的「例外」包含 Java/JNI pending exception，但 C layer 的 error return、HTTP status、cancellation 與 process-fatal condition 必須維持不同語意。

## 1. 核心結論

1. C 不建立 C++ 式跨層 polymorphic exception hierarchy；native layer 以明確 status/return code 為主。
2. JNI layer 把 pending Java exception 視為獨立控制狀態。每個可能造成 pending exception 的 JNI operation 都必須在規定的檢查邊界確認 exception state，再決定 translate、propagate 或 abort。
3. Java Servlet exception 屬 Java container/application semantics；不能把 Java Throwable 的內部實作細節直接暴露成 C ABI。
4. HTTP status 是 protocol outcome，不是通用 error taxonomy。400、503、502、504、500 可以各自對應不同 failure source。
5. cancellation、timeout、client disconnect 是 lifecycle/control events，不應全部包裝成 exception。
6. invariant violation、memory corruption、不可恢復 JVM/native failure 必須走 fatal/controlled-shutdown path，不得假裝成普通 request failure。
7. 所有 error path 都必須與 success path 一樣證明 ownership、lifetime、cleanup、observability 與 exactly-once terminal transition。

## 2. Nginx 交叉比對

固定 Nginx 1.30.4 的 HTTP API 將 NGX_OK、NGX_AGAIN、NGX_DONE、NGX_ERROR 與 HTTP status 分開。ngx_http_finalize_request() 負責 request termination，而 ngx_http_special_response_handler() 負責把 HTTP error code 轉成特殊回應/錯誤頁等 request outcome。

固定版本來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http.h
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_request.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_special_response.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_core_module.c

重要設計啟示：
- handler result 必須和 request state/lifecycle 一起解讀。
- error response 不必然等同於立即 close connection。
- error handling 可以影響 keep-alive、lingering close、request-body disposal 與 response generation。
- HTTP error processing 應集中於 request lifecycle，而不是散落到每個 parser/handler。

## 3. Apache HTTP Server 交叉比對

Apache httpd 2.4 的 request processing 明確要求 main request、subrequest 與 internal redirect 共用核心 processing authority；官方 developer documentation 警告不要複製 ap_process_request_internal()，避免安全模型因 duplicated logic 而分叉。

ErrorDocument 又把「發生 HTTP error」與「如何產生錯誤回應」分開：可以是預設錯誤訊息、客製訊息、本地 internal redirect 或 external redirect。

來源：
https://httpd.apache.org/docs/current/en/developer/request.html
https://httpd.apache.org/docs/2.4/mod/core.html
https://httpd.apache.org/docs/2.4/custom-error.html

目前 Apache httpd 最新 GA 為 2.4.68，發布日為 2026-06-08。
https://httpd.apache.org/download
https://dlcdn.apache.org/httpd/Announcement2.4.html

Ckarta 應吸收的是「單一 request processing authority + 分層 error response」原則，而不是複製 Apache API。

## 4. Tomcat 交叉比對

Tomcat 11.0.25 的 error handling 位於多層：Coyote protocol/error state、Catalina pipeline/Java exception、ErrorReportValve response generation。其 API 具有 ErrorState 與 ErrorReportValve 等分層。

固定版本來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/coyote/http11/Http11Processor.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/connector/CoyoteAdapter.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/valves/ErrorReportValve.java

Tomcat 官方文件指出，預設 ErrorReportValve 可能暴露 server version、stack trace 或 JSP source code；因此 error reporting 本身就是 security boundary。
https://tomcat.apache.org/tomcat-11.0-doc/security-howto.html
https://tomcat.apache.org/tomcat-11.0-doc/config/valve.html

## 5. JNI 例外機制

Java SE 21 JNI 規格明確區分 Throw/ThrowNew、ExceptionOccurred、ExceptionDescribe、ExceptionClear、ExceptionCheck 與 FatalError。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

因此 Ckarta JNI code 必須形成明確的 checked-call discipline：
- JNI method/object/buffer/reference operation 完成後，若該操作可能產生 pending exception，必須在允許的下一個 JNI operation 前檢查。
- ExceptionDescribe 是 debugging convenience，不應被當成 production exception transport。
- ExceptionClear 只能在已決定由 native layer 接管並轉譯該 exception 時使用；不能為了「讓 JNI 繼續跑」而無條件清除。
- exception object 不得被當成可跨 thread、queue 或 request lifetime 長期保存的 C handle，除非另有合法 GlobalRef ownership protocol。
- JNI failure 與 Java application exception 必須能在 C side 保留不同分類。

## 6. 建議 taxonomy

| 類別 | 典型來源 | Ckarta 語意 | 預設處理 |
|---|---|---|---|
| INPUT | parser/config input | 外部輸入不合法 | reject；適當 HTTP 4xx 或 config failure |
| POLICY | ACL/route/resource policy | 受策略拒絕 | reject/backpressure |
| RESOURCE | queue/buffer/thread/connection cap | 容量耗盡 | bounded rejection/backpressure/503 等 |
| TIMEOUT | connection/upstream/async/JVM task | 時間預算耗盡 | cancel/response/close，依 phase 決定 |
| CANCELLATION | disconnect/shutdown/AsyncContext cancel | lifecycle control event | stop work；terminal once |
| UPSTREAM | proxy/FastCGI/CGI/TLS peer | 外部服務失敗 | 502/504/close；retry 僅在語意允許時 |
| APPLICATION | Servlet Throwable | Java application failure | Servlet/container error semantics |
| JNI | pending exception/invocation/buffer/reference failure | 跨層整合失敗 | translate to stable native result 或 abort current operation |
| INTERNAL | broken invariant/implementation bug | server correctness failure | log/metric；fail closed |
| FATAL | unrecoverable JVM/native/process state | process integrity failure | controlled shutdown / exit |

HTTP status 不得取代上述 taxonomy；同一 503 可來自 resource saturation、maintenance 或 upstream policy。

## 7. Propagation architecture

推薦唯一主路徑：

C I/O/parser/policy
→ native status + local diagnostic context
→ request/connection state transition
→ optional JNI semantic handoff
→ Java normal result 或 Throwable
→ Java container classification
→ completion/error result
→ C owner
→ final HTTP response / close / shutdown

每一層只應有一個主要 error authority。global mutable error、return code、exception、callback 四種通道若同時存在，必須逐一定義 ownership、ordering 與 precedence，不能依 race/order 猜測。

## 8. Async exception / cancellation

非同步 request 的 error 不能只表達成同步 return -1。至少需要：

OP_PENDING
→ COMPLETE | FAIL | CANCEL
→ publish exactly one terminal outcome
→ release/transfer owner

late completion、duplicate completion、cancelled request 與 owner teardown 必須能決定性處理。

注意：cancellation 不等於 exception。client disconnect 之後，Java task 可以收到 cancellation/complete semantics；native layer 不應為了統一形式而製造虛假的 application exception。

## 9. JNI ↔ Java exception translation

只允許在真正的 ABI boundary 做 translation。

native → Java：
- 檢查 native preconditions。
- 呼叫 JNI。
- 若 Java exception pending，先保存「發生於哪個 operation」的穩定診斷 context。
- 若 error 可恢復，將 exception 摘要轉換為 stable native status；清除 pending exception 前必須已決定責任歸屬。

Java → native：
- 不暴露 Throwable hierarchy、stack frame、HotSpot internal class 等 implementation detail。
- completion record 應帶 stable category/code/status；詳細 Java exception 留在 Java logging/diagnostic layer。

## 10. Executor rejection

目前 Ckarta Java side 使用 bounded ThreadPoolExecutor + AbortPolicy。RejectedExecutionException 的正確分類是 RESOURCE/OVERLOAD，而不是 INTERNAL 或 APPLICATION exception。

正式 ABI 應明確定義 rejection 是「尚未進入 application execution」的資源事件；因此不得產生看似 Servlet exception 的假象。

## 11. Error response 與資訊洩漏

內部診斷與 client-visible response 必須分開。

client 預設只得到必要 HTTP status 與最小、sanitized body；server logs 才保留 exception class/cause、native status、phase、correlation id 等詳細資訊。

不得把以下資料預設送給 client：
- Java stack trace
- server version / build identifiers
- filesystem path
- native pointer/address
- internal queue/thread identifiers
- TLS private material
- credential/token
- upstream internal topology

Tomcat 11.0.25 的官方 security guidance 已把 ErrorReportValve 資訊暴露列為安全考量，因此 Ckarta 應把 error disclosure policy 納入核心設計。

## 12. Retry

exception/error category 本身不能觸發 retry。retryability 必須同時考慮：
- operation semantics
- idempotency
- bytes already sent
- request-body replayability
- upstream state
- timeout budget
- cancellation state

尤其 proxy path 不得因 upstream exception 就自動重試非冪等請求。

## 13. Observability

每個跨層 terminal failure/cancellation 至少應能關聯：
- request_id
- connection_id（存在時）
- owner/lifetime identity
- phase
- error category
- stable error code
- HTTP status（已決定時）
- retryability
- terminal/cancelled state

metrics 至少分開 parse errors、policy rejects、resource exhaustion、timeouts、cancellations、upstream failures、Java application exceptions、JNI failures、invariant violations、fatal shutdowns。

## 14. Academic grounding

### 14.1 Structured exception semantics

John B. Goodenough, “Structured Exception Handling”, POPL 1975, pp. 204–224, DOI 10.1145/512976.512997；Barbara H. Liskov and Alan Snyder, “Exception Handling in CLU”, IEEE Transactions on Software Engineering 5(6), 1979, pp. 546–558, DOI 10.1109/TSE.1979.230191。兩者都把 exception 視為結構化的非局部控制流，並把 handler behavior 與模組可靠性／程式結構聯繫起來。

來源：
https://doi.org/10.1145/512976.512997
https://doi.org/10.1109/TSE.1979.230191

### 14.2 Java JIT exception implementation

SeungIl Lee、Byung-Sun Yang、Soo-Mook Moon 等人的 Java JIT 研究指出，exception handlers 會影響 JIT optimization，並提出 on-demand translation 等方法，以降低正常路徑受 exception machinery 影響的程度。這支持 Ckarta 將 application exception handling 限制在 Java semantic plane，而不要把它擴散到 native HTTP hot path。

可核實書目：SeungIl Lee, Byung-Sun Yang, Suhyun Kim, Seongbae Park, Soo-Mook Moon, Kemal Ebcioǧlu, Erik R. Altman, “Efficient Java exception handling in just-in-time compilation”, Java Grande 2000, pp. 1–8, DOI 10.1145/337449.337453；期刊擴充版：SeungIl Lee, Byung-Sun Yang, Soo-Mook Moon, Software: Practice and Experience 34(15), 2004, pp. 1463–1480, DOI 10.1002/spe.622。

來源：
https://doi.org/10.1145/337449.337453
https://doi.org/10.1002/spe.622

### 14.3 Zero-overhead principle

Cong Ma、Zhaoyi Ge、Max Jung、Yizhou Zhang, “Zero-Overhead Lexical Effect Handlers”, Proceedings of the ACM on Programming Languages 9(OOPSLA2), Article 399, 2025, pp. 3533–3559, DOI 10.1145/3763177。該研究明確討論正常路徑與 exceptional path 的成本分離。

來源：
https://doi.org/10.1145/3763177
https://cs.uwaterloo.ca/~yizhou/papers/zero-oopsla2025.pdf

Ckarta 的含義不是「所有 exception 都零成本」，而是正常 HTTP hot path 不應為低頻 Java exception 增加不必要的跨層 machinery。

### 14.4 Recovery and dependability

David Patterson、Aaron Brown、Pete Broadwell、George Candea、Mike Chen、James Cutler、Patricia Enriquez、Armando Fox、Emre Kiciman、Matthew Merzbacher、David Oppenheimer、Naveen Sastry、William Tetzlaff、Jonathan Traupman、Noah Treuhaft, “Recovery-Oriented Computing (ROC): Motivation, Definition, Techniques, and Case Studies”, U.C. Berkeley Computer Science Technical Report UCB/CSD-02-1175, 2002。George Candea、Aaron B. Brown、Armando Fox、David Patterson, “Recovery-Oriented Computing: Building Multitier Dependability”, IEEE Computer 37(11), 2004, pp. 60–67, DOI 10.1109/MC.2004.219。這些工作支持把 failure detection、diagnosis、recovery 與 state preservation 視為系統架構責任，而不只是語言層 exception syntax。

來源：
https://www2.eecs.berkeley.edu/Pubs/TechRpts/2002/5574.html
https://doi.org/10.1109/MC.2004.219

## 15. 與現有 Ckarta lifecycle 的整合

所有 exception/error/cancel paths 都必須與 `docs/CONNECTION_OWNERSHIP.md`、`docs/CANCELLATION_MODEL.md`、`docs/JNI_ABI.md`、`docs/CONCURRENCY_MODEL.md` 相容。

尤其：
- Java 持有 DirectByteBuffer view 時，C owner 不得提早 recycle buffer。
- completion 發布前必須仍存在合法 owner/lifetime context。
- cancellation 後的 late completion 必須被辨識並不得再次終止或釋放 request。
- JVM DestroyJavaVM 前不得仍有可能進入 JNI 的 native thread。

## 16. Implementation gates

正式實作前至少要完成：
1. error category/code registry。
2. request/connection state machine 中的 terminal/error transitions。
3. JNI checked-call convention。
4. stable Java exception translation boundary。
5. client error-disclosure policy。
6. exactly-once completion/error/cancellation contract。
7. logging failure 與 error-path memory-safety tests。
8. JNI exception injection tests。
9. executor saturation/rejection tests。
10. timeout、disconnect、AsyncContext、upstream failure、shutdown race tests。
11. fuzz/negative tests 及 sanitizer coverage。

## 17. 目前 `ck_error` 狀態

已建立第一個 process-local `ck_error_t` 骨架及 layout test；它不是 Java exception hierarchy，也不是 externally loadable plugin ABI。它目前刻意沒有直接嵌入 `ck_request_t`，因為 request state 與 error payload 的 concurrent publication 若沒有 terminal publication primitive 會產生 data race。

正式接入 request/completion 前，必須先完成 `docs/ERROR_STATE_MATRIX.md` 定義的 exactly-once publication、late completion、cancellation race 與 owner teardown contract。
