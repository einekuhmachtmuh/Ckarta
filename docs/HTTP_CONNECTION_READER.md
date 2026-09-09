# Ckarta HTTP Connection Reader 契約

## 1. 目的

`c/http/ck_http_connection_reader.[ch]` 是 HTTP/1.1 request input 與 Linux/POSIX non-blocking socket read 之間的 bounded connection-owned consumer slice。

它不是 HTTP parser 的第二套語意，也不是 Servlet request facade。HTTP framing 仍唯一由 `ck_http_parser`、`ck_http_input` 與 chunked decoder 組成；reader 只負責把 socket bytes 交給該 framing pipeline，維護尚未消耗的 connection input，並在 request 完成時保留後續 pipelined bytes。

## 2. Ownership

```text
ck_connection owner
    │
    └── heap-backed connection reader
            ├── fixed receive buffer
            └── ck_http_input state
```

目前 `ck_http_connection_reader_t` 不是按值嵌入 `ck_connection_t`；`ck_connection_t` 持有其 heap allocation 的唯一 owner pointer，並在 `ck_connection_init()` 建立，在 connection terminal close 成功後釋放。`ck_connection_http_reader()` 只向已取得 connection ownership 的 caller 提供 borrow pointer。

reader buffer 由 connection-side owner 擁有。body sink callback 只借用 body span，必須在 callback 返回前完成同步消費；callback 不得保存該 pointer 到下一次 reader operation。

`ck_http_request_t` 的 spans 受 `ck_http_input`／parser storage lifetime 約束。`ck_http_connection_reader_next_request()` 會 recycle current request state，因此 caller 不得在其後繼續使用舊 request span。

registry 不直接暴露內部 `ck_connection_t *` 給未受控 caller；正式 registry dispatch 使用 reader pin：`ck_connection_registry_reader_acquire()` 在 registry lock 內完成 handle/generation/identity validation 並增加 reader user count，然後 caller 可在不持有 registry lock 的情況下執行 non-blocking reader operation；`ck_connection_registry_reader_release()` 減少 user count。只要 reader pin 存在，registry 不允許 close 或 retire，以避免 reader 與 underlying connection entry 在使用期間被回收。

reader access 仍遵循 connection owner single-owner contract：同一時間只有受授權的 connection owner 應推進 reader state。reader pin 是 lifetime guard，不是 reader 本身的 general-purpose thread-safe guarantee。

## 3. Read contract

`ck_http_connection_reader_drive()` 使用 `recv(..., MSG_DONTWAIT)`：

- `> 0`：取得新的 socket bytes，放入 reader-owned buffer。
- `0`：peer EOF；若 current request 尚未完成，回傳 `CK_HTTP_CONNECTION_READ_EOF_INCOMPLETE`。
- `EAGAIN/EWOULDBLOCK`：目前沒有更多 bytes，若 request 尚未完成則回傳 `CK_HTTP_CONNECTION_READ_INCOMPLETE`。
- `EINTR`：重試同一 read operation。
- 其他 error：回傳 `CK_HTTP_CONNECTION_READ_IO_ERROR`。

reader 不在 `EAGAIN` 上阻塞，也不等待 Servlet application code。

為避免單一繁忙 connection 在 Linux level-triggered readiness 下長時間佔用 event-loop iteration，單次 `ck_http_connection_reader_drive()` 同時受兩個獨立 budget 約束：最多讀取 `32 KiB` socket bytes，以及最多處理 `32 KiB` 已緩衝 input bytes。任何一個 budget 耗盡且 request 尚未完成時，reader 回傳 `CK_HTTP_CONNECTION_READ_INCOMPLETE`；在後續 readiness/dispatch 再繼續。此 budget 是 Ckarta 的公平性與過載控制策略，不是 HTTP 語意限制。

若 framing state 在目前 buffer 中合法地停在尚待後續 byte 的邊界，例如 chunked data 的 `CR` 已收到但 `LF` 尚未到達，reader 必須保留該 byte 並回報 `INCOMPLETE`，不可把 zero-progress feed 視為 I/O error。若 framing 在該 input slice 沒有消耗任何 byte，reader 會先停止目前 parse batch，再嘗試取得後續 socket bytes；不得在相同 buffer 上無限重試。

## 4. Input consumption

每次呼叫 `ck_http_input_feed()` 時，`consumed` 是當前 buffer slice 的 relative byte count。

reader 只有在 sink callback 成功後才前移 `begin`。因此 sink failure 不會在 native reader 層假裝 body 已被成功消費；caller 應將該狀態視為 request/connection error path。

當 request 完成而 `begin < end` 時，剩餘 bytes 屬於同一 TCP connection 上尚未解析的下一則 request；reader 不丟棄這些 bytes。

`ck_http_connection_reader_next_request()` 只有在 current request 已完成後才能呼叫；成功回傳 `0`，無效 reader 或 current request 未完成則回傳 `-1`。它將 leftover bytes compact 到 buffer 前端，再重設 `ck_http_input` parser/body state。

## 5. Bounded memory

reader receive buffer 固定為 `65536` bytes；header parser 自身上限仍為 `32768` bytes，chunked line/trailer bounds 仍由 `ck_http_chunked` 控制。

因此 reader buffer 大小不是 HTTP header limit，也不是 decoded request-body limit。大型 body 可透過 body sink 分段消費，不要求一次容納整個 body。

Header 或 chunk framing 若先觸發其自身 bound，reader 直接回傳 `TOO_LARGE`，不得透過擴張 reader buffer 繞過 protocol-level limit。

## 6. Nginx/Tomcat cross-check

Nginx 1.30.4 fixed source 的一般 read handler 可在 ready 狀態持續讀取直到 `EAGAIN`；其 HTTP request path 同時以 request/connection state、request buffer 與 request-body buffering 管理目前 request 與剩餘 input。Ckarta reader 因採 Linux level-triggered event backend，不直接複製 Nginx「單次 read handler 讀到 EAGAIN」的無上限 batch 行為，而是加上明確 per-dispatch work budget，同時保留 connection buffer 中 current request／leftover 分界的設計思想。

Tomcat 11.0.25 fixed `NioEndpoint.Poller.processKey()` 將 readable event 交給 socket processor；`Http11InputBuffer` 另有目前輸入 buffer、request boundary 與 `nextRequest()`／recycle 生命周期。Ckarta reader 因而把「I/O readiness → bounded native processing → 明確 request recycle」拆成可驗證的 native contract，而不是將 Java Poller／processor 模型直接搬到 C。

固定來源：

Nginx 1.30.4
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_request.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_request_body.c

Tomcat 11.0.25
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/coyote/http11/Http11InputBuffer.java

## 7. HTTP authority

RFC 9112 對 request message body framing 以 `Transfer-Encoding`、`Content-Length` 與無兩者時的 zero-length body 規則決定 message boundary；有效 Content-Length 未收足即屬 incomplete request，而 chunked 必須逐 chunk 解碼至 terminating zero-size chunk 與 trailer section 完成。

官方來源：https://www.rfc-editor.org/rfc/rfc9112.html

Ckarta reader 不重新解釋這些規則，只執行已由 framing layer 決定的 boundary。

## 8. Current status

已完成：

- bounded connection-owned receive buffer
- `ck_connection_t` 持有 heap-backed reader owner pointer
- registry-safe reader pin acquire/release contract
- non-blocking `recv()` consumer
- bounded per-dispatch read/process work budget
- HTTP header/body framing composition
- Content-Length streaming body sink
- chunked streaming body sink
- leftover/pipelined byte preservation
- explicit request recycle
- explicit request recycle return contract
- EOF / EAGAIN / EINTR / socket-error mapping
- body sink failure boundary
- fragmented chunk delimiter regression test
- bounded dispatch regression test
- independent reader socketpair tests
- connection close 後 reader lifetime invalidation test
- registry close/retire 被 active reader pin 阻擋的測試
- loopback TCP integration 透過 registry-safe reader pin 消費 request body 並處理 pipelined request

尚未完成：

- request timeout／Slowloris timer source
- true multi-worker accept ownership
- production connection read-event state 的完整 lifecycle state machine
- response/output state machine
- Servlet request-body stream adapter
- async cancellation integration
- TLS socket integration
- graceful shutdown drain
- Servlet 6.1 TCK compatibility tests
- security/fuzz coverage for full HTTP compatibility

因此此文件的「已完成」只表示 connection-owned reader slice；不得解讀為完整 production HTTP server。

## 9. 理論依據

SEDA：Matt Welsh、David Culler、Eric Brewer, “SEDA: an architecture for well-conditioned, scalable internet services”, ACM SIGOPS Operating Systems Review 35(5), 230–243, 2001. DOI: https://doi.org/10.1145/502059.502057

此處只採其 event-driven stage 與 bounded-resource control 的架構思想，不宣稱 reader 本身是 SEDA implementation；32 KiB budget 是 Ckarta 自己的工程參數，仍應透過 workload benchmark 驗證。

Herlihy/Wing：Maurice P. Herlihy、Jeannette M. Wing, “Linearizability: A Correctness Condition for Concurrent Objects”, ACM Transactions on Programming Languages and Systems 12(3), 463–492, 1990. DOI: https://doi.org/10.1145/78969.78972

此處用作 concurrent ownership／terminal arbitration 與 lifetime guard 的 correctness reasoning baseline；reader 本身沒有因此被標示為 thread-safe shared object。
