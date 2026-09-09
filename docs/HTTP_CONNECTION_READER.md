# Ckarta HTTP Connection Reader 契約

## 1. 目的

`c/http/ck_http_connection_reader.[ch]` 是 HTTP/1.1 request input 與 Linux/POSIX non-blocking socket read 之間的 bounded connection-owned consumer slice。

它不是 HTTP parser 的第二套語意，也不是 Servlet request facade。HTTP framing 仍唯一由 `ck_http_parser`、`ck_http_input` 與 chunked decoder 組成；reader 只負責把 socket bytes 交給該 framing pipeline，維護尚未消耗的 connection input，並在 request 完成時保留後續 pipelined bytes。

## 2. Ownership

```text
ck_connection owner
    │
    └── connection reader
            ├── fixed receive buffer
            └── ck_http_input state
```

reader buffer 由 connection-side owner 擁有。body sink callback 只借用 body span，必須在 callback 返回前完成同步消費；callback 不得保存該 pointer 到下一次 reader operation。

`ck_http_request_t` 的 spans 受 `ck_http_input`／parser storage lifetime 約束。`ck_http_connection_reader_next_request()` 會 recycle current request state，因此 caller 不得在其後繼續使用舊 request span。

## 3. Read contract

`ck_http_connection_reader_drive()` 使用 `recv(..., MSG_DONTWAIT)`：

- `> 0`：取得新的 socket bytes，放入 reader-owned buffer。
- `0`：peer EOF；若 current request 尚未完成，回傳 `CK_HTTP_CONNECTION_READ_EOF_INCOMPLETE`。
- `EAGAIN/EWOULDBLOCK`：目前沒有更多 bytes，若 request 尚未完成則回傳 `CK_HTTP_CONNECTION_READ_INCOMPLETE`。
- `EINTR`：重試同一 read operation。
- 其他 error：回傳 `CK_HTTP_CONNECTION_READ_IO_ERROR`。

reader 不在 `EAGAIN` 上阻塞，也不等待 Servlet application code。

## 4. Input consumption

每次呼叫 `ck_http_input_feed()` 時，`consumed` 是當前 buffer slice 的 relative byte count。

reader 只有在 sink callback 成功後才前移 `begin`。因此 sink failure 不會在 native reader 層假裝 body 已被成功消費；caller 應將該狀態視為 request/connection error path。

當 request 完成而 `begin < end` 時，剩餘 bytes 屬於同一 TCP connection 上尚未解析的下一則 request；reader 不丟棄這些 bytes。

`ck_http_connection_reader_next_request()` 只有在 current request 已完成後才能呼叫。它將 leftover bytes compact 到 buffer 前端，再重設 `ck_http_input` parser/body state。

## 5. Bounded memory

reader receive buffer 固定為 `65536` bytes；header parser 自身上限仍為 `32768` bytes，chunked line/trailer bounds 仍由 `ck_http_chunked` 控制。

因此 reader buffer 大小不是 HTTP header limit，也不是 decoded request-body limit。大型 body 可透過 body sink 分段消費，不要求一次容納整個 body。

Header 或 chunk framing 若先觸發其自身 bound，reader 直接回傳 `TOO_LARGE`，不得透過擴張 reader buffer 繞過 protocol-level limit。

## 6. Nginx/Tomcat cross-check

Nginx 1.30.4 fixed source 的 request-body path 使用 preread bytes、request-body filter、remaining body tracking，以及 request body 結束後保存 pipelined header 的設計；Ckarta reader 只採其「同一 connection buffer 中區分 current request 與 leftover input」的原則，不複製 Nginx private structures。

Tomcat 11.0.25 fixed `Http11InputBuffer` 明確把 `end` 定義為 header 結束及 body 起點，並由 `nextRequest()` 保存 leftover bytes、重設 current request parser state。Ckarta reader 因而將 `next_request()` 視為明確 lifecycle operation，而非自動隱式 recycle。

固定來源：

Nginx 1.30.4
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_request.c

Tomcat 11.0.25
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/coyote/http11/Http11InputBuffer.java

## 7. HTTP authority

RFC 9112 對 request message body framing 明確以 `Transfer-Encoding`、`Content-Length` 與無兩者時的 zero-length body 規則決定 message boundary；有效 Content-Length 未收足即屬 incomplete request，而 chunked 必須逐 chunk 解碼至 terminating zero-size chunk 與 trailer section 完成。citeturn933093search0turn933093search24

Ckarta reader 不重新解釋這些規則，只執行已由 framing layer 決定的 boundary。

## 8. Current status

已完成：

- bounded connection-owned receive buffer
- non-blocking `recv()` consumer
- HTTP header/body framing composition
- Content-Length streaming body sink
- chunked streaming body sink
- leftover/pipelined byte preservation
- explicit request recycle
- EOF / EAGAIN / EINTR / socket-error mapping
- body sink failure boundary
- unit tests
- loopback TCP integration

尚未完成：

- `ck_http_connection_reader_t` 直接嵌入／由 `ck_connection_t` 正式擁有
- production connection state enum integration
- request timeout／Slowloris timer source
- true multi-worker accept ownership
- response/output state machine
- Servlet request-body stream adapter
- async cancellation integration
- TLS socket integration
- graceful shutdown drain

因此此文件的「已完成」只表示 executable reader slice；不得解讀為完整 production HTTP server。

## 9. 理論依據

SEDA 將高並行 Internet service 分解為 event-driven stages，並以顯式 queue／resource control 管理負載；Ckarta 在此處只採分層與 bounded-resource 思想，不宣稱 reader 是 SEDA implementation。citeturn215109search0turn215109search25

Herlihy/Wing 的 linearizability 提供 concurrent object 的 correctness reasoning baseline；reader 本身目前由單一 connection owner 驅動，因此沒有將其標示為 thread-safe shared object。citeturn215109search23turn215109search1
