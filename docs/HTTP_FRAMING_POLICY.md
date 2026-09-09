# Ckarta HTTP/1.1 Framing 政策

## 1. 目標

Ckarta 必須建立唯一的 HTTP/1.1 message framing（訊息框架）權威解析語意。

所有 frontend、Servlet dispatch、reverse proxy（反向代理）與 upstream forwarding（上游轉送）都必須依賴同一套 framing result（訊息框架解析結果）。

## 2. 規格來源

RFC 9112：
https://www.rfc-editor.org/rfc/rfc9112.html

本文件不得取代 RFC。

## 3. Nginx 與 Tomcat 對照

Nginx：

third_party/nginx/src/http/ngx_http_request.c

相關函式：

ngx_http_process_request_line()
ngx_http_process_request_headers()
ngx_http_process_request()

Tomcat：

third_party/tomcat/java/org/apache/coyote/http11/Http11Processor.java
third_party/tomcat/java/org/apache/coyote/http11/Http11InputBuffer.java

相關方法：

service()
parseRequestLine()
parseHeaders()
prepareRequest()

兩者都將 HTTP parsing（HTTP 解析）集中於 protocol layer（協定層），而不是交給 Servlet application。

## 4. Ckarta parser 邊界

parser input：

pointer + length

parser output 至少包括：

- method
- target
- version
- normalized headers
- content length semantics（內容長度語意）
- transfer coding semantics（傳輸編碼語意）
- body framing state
- consumed bytes
- parse result

目前 executable slice 已落地 `c/http/ck_http_parser.[ch]`。parser 只擁有自己的 bounded header buffer；輸入可以分多次 feed，header 區塊完成後回傳本 request 的 consumed bytes，使 caller 能保留後續 body／pipeline bytes。

目前 parser 已完成 request-line、header-field grammar、Content-Length normalization、Transfer-Encoding final-coding 判斷與 header-size bound；chunked body 的 chunk-size/data/trailer 解碼仍未完成。

## 5. 嚴格要求

遇到 framing ambiguity（訊息框架歧義）時：

reject。

不可：

- C parser 採一種 interpretation（解讀）
- Java parser 採另一種
- proxy parser 再採第三種

## 6. Content-Length

必須明確驗證：

- 缺失
- 單值
- 重複值
- 不一致值
- 非法數字
- overflow（溢位）

RFC 9112 允許收到多個 `Content-Length` 時，在可解析為逗號分隔 list 且所有值相同的情況下，以單一值處理；Ckarta parser 已採此 normalization，但任何不一致值都拒絕。

在進入 proxy 或 Servlet 前必須完成 normalized result（正規化結果）。

## 7. Transfer-Encoding

必須依 RFC 9112 處理：

- 缺失
- token
- chunked
- 非法值
- 與 Content-Length 的互動

目前 Ckarta executable parser 的 framing consumer 只把最終 `chunked` 視為可接受的 request transfer framing；任何最終不是 `chunked` 的 request transfer coding 均拒絕。非 `chunked` transfer-coding 的實際 decoding 尚未實作，因此不能把此 parser slice 標示成完整 Transfer-Encoding implementation。

若同時存在 `Transfer-Encoding` 與 `Content-Length`，Ckarta 使用 Transfer-Encoding 決定 framing，並設定 `connection_close_required`；這與 RFC 9112 對 request-smuggling risk 的處理方向一致，但正式 response/status 行為仍需接入完整 request state machine。

## 8. Chunked body

chunk size 必須：

- 有界
- 完整
- checked arithmetic（檢查過的算術）
- 不允許越界
- 不允許 parser state desynchronization（解析器狀態去同步）

目前只完成 header framing 的 `chunked` mode recognition；chunk data、CRLF、last-chunk、trailer section 與 truncated-body handling 尚待下一個獨立 parser state-machine gate。

## 9. Proxy

Ckarta 在轉送前應形成自己的 canonical request representation（正規請求表示）。

upstream parser 不得重新詮釋同一個模糊 framing。

## 10. 測試

目前 `tests/http/ck_http_parser_test.c` 已驗證：

- incremental feed
- request line token validation
- Content-Length 正常值
- identical duplicate Content-Length normalization
- conflicting Content-Length rejection
- Transfer-Encoding + Content-Length interaction
- non-chunked final Transfer-Encoding rejection
- oversized header buffer

TCP integration test 另直接由 loopback accepted socket 讀取 HTTP bytes，再送入同一 parser，驗證 parser 並非只存在於 isolated unit test。

仍需補齊：

- duplicate Content-Length
- conflicting Content-Length
- Transfer-Encoding variants
- malformed chunk size
- truncated chunk body
- extra bytes after body
- oversized headers
- oversized body
- pipelined requests
- keep-alive boundary

所有 corpus（測試語料）都應保留 regression identifier（回歸識別碼）。

## 11. 安全

HTTP request smuggling（HTTP 請求走私）視為 parser architecture issue（解析器架構問題），不是獨立的 proxy feature。

這是 Ckarta 的 architecture invariant。
