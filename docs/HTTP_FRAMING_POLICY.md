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

目前 executable slice：

`c/http/ck_http_parser.[ch]`
`c/http/ck_http_chunked.[ch]`
`c/http/ck_http_input.[ch]`

header parser 以 bounded buffer 做增量 feed；header 區塊完成後，`consumed` 表示該次 feed 實際消耗的 header bytes，body 與後續 pipelined input 不被 parser 宣稱為已消耗。

parser 現在只複製至實際 `\r\n\r\n` framing boundary，不再因單次 `feed` 同時帶入大量 body bytes 而把 body 誤計入 header buffer 上限。

`ck_http_input` 進一步把 header framing、Content-Length body progress 與 chunked decoding 串成單一 per-request input state。它只回傳目前 feed 內可立即借用的 body span，並以 `consumed` 明確保留尚未屬於當前 request message 的剩餘 input；request 結束後由 caller 顯式建立下一 request state。

header parser 已完成 request-line、header-field grammar、Content-Length normalization、Transfer-Encoding 判斷與 header-size bound。chunked body 則由 decoder 維護 `size → data → data CRLF → trailers → done` 狀態，body data 以 input span 直接交給 caller，不建立額外 body copy。

目前以上是 bounded executable components；尚未等同正式 multi-worker connection HTTP production loop。

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

目前 Ckarta executable header parser 的 framing consumer 只接受 `chunked` 為可解碼 request transfer coding；其他 transfer coding 目前直接拒絕，因為 corresponding decoder 尚未加入。這是刻意的 safety boundary，不是完整 Transfer-Encoding 相容性宣稱。

若同時存在 `Transfer-Encoding` 與 `Content-Length`，Ckarta 使用 Transfer-Encoding 決定 framing，並設定 `connection_close_required`；完整 HTTP request error response／connection close policy 仍需接入 production connection state machine。

## 8. Chunked body

RFC 9112 要求 recipient 能解析並解碼 chunked transfer coding，且必須防止大 hexadecimal chunk size 導致 integer overflow 或 precision loss。

目前 `c/http/ck_http_chunked.[ch]` 已提供 bounded incremental decoder：

- chunk-size hexadecimal parsing
- chunk extension syntax bound
- chunk data span output
- chunk data trailing CRLF validation
- zero-size last chunk
- trailer field syntax validation
- trailer count／byte bounds
- decoded-body overflow checking
- incremental input consumption

`c/http/ck_http_input.[ch]` 現已將此 decoder 接入 per-request input state machine，並由 `tests/http/ck_http_input_test.c` 驗證 Content-Length／chunked、fragmentation、body boundary 與 subsequent request bytes 的 consumed boundary。

尚未完成：

- input state machine 與正式 connection event consumer 的 ownership integration
- configurable maximum decoded body size
- trailer storage／forwarding policy
- complete request-to-Servlet body stream mapping
- production request recycle 與 keep-alive loop

## 9. Proxy

Ckarta 在轉送前應形成自己的 canonical request representation（正規請求表示）。

upstream parser 不得重新詮釋同一個模糊 framing。

## 10. 測試

`tests/http/ck_http_parser_test.c` 已驗證：

- incremental header feed
- request line token validation
- Content-Length 正常值
- identical duplicate Content-Length normalization
- conflicting Content-Length rejection
- Transfer-Encoding + Content-Length interaction
- non-chunked final Transfer-Encoding rejection
- oversized header buffer

`tests/http/ck_http_chunked_test.c` 覆蓋：

- incremental chunk-size/data/trailer feed
- chunk extension
- zero-size last chunk
- trailer field
- chunk-size overflow rejection
- malformed line ending rejection
- truncated chunk data state

`tests/http/ck_http_input_test.c` 覆蓋：

- header／body 同一 feed
- Content-Length fragmented body
- premature EOF state
- chunked fragmented body
- body completion 與 subsequent request boundary
- no-body request completion

TCP integration test 仍是 loopback accepted socket → HTTP header parser 的 executable slice；`ck_http_input` 尚未接入正式 connection body event loop。

仍需補齊：

- multiple Transfer-Encoding variants
- malformed chunk extensions
- truncated chunk CRLF
- trailer policy corpus
- oversized decoded body policy
- pipelined requests in the production connection loop
- keep-alive boundary in the production connection loop
- HTTP request smuggling corpus

所有 corpus（測試語料）都應保留 regression identifier（回歸識別碼）。

## 11. 安全

HTTP request smuggling（HTTP 請求走私）視為 parser architecture issue（解析器架構問題），不是獨立的 proxy feature。

這是 Ckarta 的 architecture invariant。
