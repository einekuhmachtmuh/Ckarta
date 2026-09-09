# Ckarta HTTP Output Writer 契約

## 1. 目的

`c/output/ck_http_output_writer.[ch]` 是 Ckarta native HTTP response transaction 與 Linux/POSIX non-blocking socket writable path 之間的 bounded output consumer slice。

本元件不決定 HTTP response status、header、chunked encoding 或 Servlet semantics；它只管理已序列化 bytes 的短期 output ownership、partial write 與 writable continuation。

## 2. Ownership

`ck_http_output_writer_t` 由持有該 connection output path 的單一 owner 管理。writer 持有 socket descriptor 的 borrow/reference value，但不負責建立或關閉 socket；socket lifetime 必須長於 writer operation。

writer 內嵌固定 65,536-byte output buffer，不配置無界 heap queue，也沒有跨 thread shared-state 契約。

caller 必須保證同一時間只有一個 owner 推進 writer state。writer 不因 non-blocking API 而自動成為 thread-safe。

## 3. Queue contract

`ck_http_output_writer_queue()` 將 caller-provided bytes 複製至 writer-owned buffer。

若目前 buffer 前端存在已消耗空間，且尾端已達容量，writer 會先 compact；若可用空間仍不足，回傳 failure，不擴張 buffer。

因此 queue failure 是 backpressure（反壓）邊界，而不是透過 malloc 無限成長來隱藏 over-capacity。

## 4. Write contract

`ck_http_output_writer_drive()` 使用 `send(..., MSG_DONTWAIT | MSG_NOSIGNAL)`，每次 dispatch 最多傳送 32 KiB。

結果：

- `DRAINED`：writer buffer 已完全排空。
- `NEED_WRITE`：仍有 bytes 待傳，caller 應保留 writer state；若由 event loop 驅動，應維持 writable interest 並在下一次 writable readiness 繼續。
- `PEER_CLOSED`：peer 已關閉或 connection reset（例如 `EPIPE`／`ECONNRESET`）。caller 必須進入 connection error／terminal policy，而不得繼續把 response 視為成功送出。
- `IO_ERROR`：其他不可恢復的 socket error。

`EINTR` 只重試原本尚未完成的 send operation。

## 5. Response transaction boundary

response transaction 的 `FINISHED` 不等於 socket bytes 已 drained。transaction completion、output drain、connection keep-alive recycle 是三個不同 boundary。

目前 writer 只實作第二個 boundary 的 native slice；它不自行改變 `ck_http_response_t` state。

## 6. Event loop integration

Linux `ck_event_loop` 以 `CK_EVENT_WRITE` 對應 `EPOLLOUT`。writer 在 `NEED_WRITE` 狀態仍保存尚未送出的 bytes，connection layer 應在下一個 writable notification 再呼叫 `drive()`。

當 writer 回傳 `DRAINED`，connection layer 可移除不必要的 `EPOLLOUT` interest；不得因 writable readiness 長期持續而每次無條件執行無界 output work。

## 7. Nginx/Tomcat cross-check

Nginx 的 `ngx_http_write_filter()` 將尚未送出的 response chain 保留在 `r->out`，呼叫 connection 的 `send_chain()`；若仍有 output pending，回傳 `NGX_AGAIN`，並保留 chain 供後續 writable processing。Ckarta 只採其「pending output 必須持續保留，write readiness 與 application response completion 必須分離」的概念，不複製 Nginx chain／pool/module ABI。

固定來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/http/ngx_http_write_filter_module.c

Tomcat 11.0.25 的 `Http11OutputBuffer` 將 response header buffer、output filters、socket output 與 `end()` 分離；其 non-blocking API 另以 `isReady()`、`registerWriteInterest()`、`hasDataToWrite()` 管理 writable state。Ckarta 因而將「response transaction」與「socket output writer」拆成兩個 native layer。

固定來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/coyote/http11/Http11OutputBuffer.java

Tomcat 的 `Response.isReady()`／`checkRegisterForWrite()` 亦把 writable interest registration 與 application callback dispatch 分開；Ckarta 下一階段仍需將此語意與 Servlet non-blocking response API 對接，而目前 writer 本身尚未做 Java callback dispatch。

固定來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/coyote/Response.java

## 8. 理論依據

SEDA 的 staged event-driven architecture 提供 bounded resource 與 stage separation 的架構參考。此處只用於說明「單一事件處理不得無界壟斷資源」的設計方向，不由論文推導 32 KiB 為最佳值。

Matt Welsh、David Culler、Eric Brewer, “SEDA: an architecture for well-conditioned, scalable internet services”, ACM SIGOPS Operating Systems Review 35(5), 230–243, 2001. DOI: https://doi.org/10.1145/502059.502057

## 9. Current status

已完成：

- fixed-capacity output buffer
- explicit socket ownership boundary
- partial `send()` continuation
- 32 KiB per-dispatch write budget
- `EAGAIN`／`EINTR`／peer-close／other-error handling
- EPOLLOUT integration regression test

尚未完成：

- HTTP status/header serialization
- response filter chain
- chunked response encoder
- response-specific HEAD／204／304／CONNECT body rules
- TLS output
- Java ServletOutputStream／Writer integration
- non-blocking Servlet `WriteListener` callback dispatch
- connection-level response recycle
- graceful shutdown output drain
