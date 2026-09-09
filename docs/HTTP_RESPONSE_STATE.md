# Ckarta HTTP Response State 契約

## 1. 目的

`c/output/ck_http_response.[ch]` 是 Ckarta native HTTP response output path 的第一個可執行 ownership／lifecycle slice。

本階段刻意只處理 response transaction state、body staging 與 Content-Length completion invariant，不直接擁有 socket、不執行 `send()`、不管理 TLS，也不直接實作 Servlet `Writer`／`ServletOutputStream`。這樣可先固定 response lifetime，再把 socket output 與 Java boundary 接上。

## 2. 狀態

```text
NEW
 │
 │ commit() 或第一次 write_body()
 ▼
COMMITTED
 │
 │ finish() 且 framing invariant 成立
 ▼
FINISHED

任一不可恢復輸出錯誤
 │
 ▼
FAILED
```

`NEW`：status、Content-Length 與 connection-close policy 尚可設定；response body 尚未視為 committed。

`COMMITTED`：不可再修改 status／Content-Length／connection-close policy；body 可繼續寫入 bounded native staging buffer。

`FINISHED`：current response transaction 已結束；不得再寫入或再次 finish。

`FAILED`：遇到 native output invariant violation，例如 body buffer overflow 或明確 Content-Length mismatch；此 state 不再接受正常 response mutation。

## 3. Ownership

`ck_http_response_t` 的所有欄位均由持有該 response transaction 的單一 owner 管理。

目前 body buffer 是 response object 內嵌的固定陣列，沒有另外配置，因此沒有額外 heap ownership。它不是 socket send queue，也沒有跨 thread sharing 契約。

後續若 Java Servlet response facade 與 native response transaction 以 handle 關聯，handle lifetime 必須短於 response owner，不能讓 Java facade 在 native response recycle 後繼續使用已失效 native state。

## 4. Commit 語意

Servlet 6.1 的 `ServletResponse` 規定：response committed 表示 status code 與 headers 已經寫出；`flushBuffer()` 會 commit；`getOutputStream()`／`getWriter()` 的 flush 也會 commit。

因此 Ckarta native transaction 把第一次 body write 視為 implicit commit，是為了保留「body output 不能在 header mutation 之後繼續任意回溯」的核心 invariant。但目前 native `commit()` 尚未真的序列化 HTTP status line／headers；這仍是下一個 output serialization slice，而不是 Servlet 6.1 完整 commit 實作。

來源：
https://jakarta.ee/specifications/servlet/6.1/apidocs/jakarta.servlet/jakarta/servlet/servletresponse

## 5. Content-Length invariant

若 response 已指定 Content-Length，`finish()` 必須驗證實際 native staging body bytes 與指定長度一致；不一致即進入 `FAILED`。

這裡沒有把 Content-Length 當成 native buffer capacity。大型 response 必須在後續階段轉化為 bounded output queue／send progress，而不是擴張單一 response object 成為無上限 buffer。

HTTP response status code 採 RFC 9110 的有效範圍 `100..599`。目前這個 final-response transaction slice 只接受 `200..599`，因此 `1xx` interim response 尚未納入此 API；這是明確的範圍限制，不是宣稱 Ckarta 已完整實作 HTTP interim response。

來源：
https://www.rfc-editor.org/rfc/rfc9110.html

## 6. Reset boundary

Servlet 6.1 允許在 response 尚未 committed 時 `reset()`，且會清除 status、headers、buffer 與 writer/output-stream selection state；一旦 committed，`reset()` 必須失敗。

Ckarta native slice 因目前尚未引入 writer/output-stream object identity，只允許 `NEW` state reset。這保留 committed boundary，但尚未等同 Servlet facade 的完整 reset semantics。

來源：https://jakarta.ee/specifications/servlet/6.1/apidocs/jakarta.servlet/jakarta/servlet/servletresponse

## 7. Nginx/Tomcat cross-check

Nginx 官方 development guide 將 HTTP response 描述為 response header 加 optional response body，兩者都可經過 filter chain，最後寫入 client socket。Ckarta 因而不把「response metadata」與「body production」視為單一無界 byte copy；目前先固定 transaction state，再接 filter／serializer／socket writer。

來源：https://nginx.org/en/docs/dev/development_guide.html

Tomcat 11.0.25 的 `Http11OutputBuffer` 管理 response header composition、active output filters、socket output、`commit()`、`end()` 與 `nextRequest()`／`recycle()`。其 `doWrite()` 在 response 尚未 committed 時要求 connector 執行 commit；其 `end()` 結束 current response output；其 `nextRequest()` recycle filter／response state 並準備下一 request。

固定來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/coyote/http11/Http11OutputBuffer.java

Ckarta 目前只吸收「commit boundary、bounded output layer、finish/recycle lifecycle」這些可驗證概念，不複製 Tomcat Java object graph。

## 8. Event-driven fairness

後續 native socket writer 應採與 reader 對稱的 bounded work policy：一次 writable dispatch 不得因某一 connection 的巨大 response 而無上限排空 output queue。

此處的設計理由可與 SEDA 的 staged event-driven architecture 中 bounded resource／well-conditioned service 的架構思想對照，但 32 KiB reader budget 或任何未來 writer budget 都是 Ckarta 自己的工程參數，必須用實際 workload benchmark 驗證，不可由 SEDA 論文直接推出「最佳」數值。

學術來源：Matt Welsh、David Culler、Eric Brewer, “SEDA: an architecture for well-conditioned, scalable internet services”, ACM SIGOPS Operating Systems Review 35(5), 230–243, 2001. DOI: https://doi.org/10.1145/502059.502057

## 9. Current status

已完成：

- explicit native response states
- bounded response body staging buffer
- status validation
- pre-commit mutation boundary
- implicit commit on first body write
- Content-Length completion validation
- terminal FAILED state on response invariant violation
- lifecycle regression tests

尚未完成：

- HTTP response status/header serialization
- response header validation and injection protection
- chunked response encoder
- HEAD／204／304／CONNECT response-specific body rules
- socket non-blocking write queue
- writable epoll integration
- response output filter pipeline
- TLS output integration
- Servlet Writer／ServletOutputStream facade
- async response completion handoff
- response recycle bound to production connection keep-alive loop

因此本文件的「已完成」只代表 native response transaction slice，不代表完整 HTTP response implementation 或 Servlet 6.1 response compatibility。
