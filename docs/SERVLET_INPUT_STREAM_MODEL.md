# Ckarta ServletInputStream / ReadListener Bridge Model

本文件是 Ckarta `ServletInputStream`、`ReadListener` 與 native request-body source 之間的 canonical design contract。它只定義目前已核准的語意與邊界，不宣稱 production integration 或 Servlet 6.1 TCK 相容已完成。

## 1. Authority

Servlet externally observable behavior 以 Jakarta Servlet 6.1 specification/API 為 normative authority。

Tomcat 11.0.25 是 implementation reference；其 `org.apache.coyote.Request` 的 non-blocking read state、listener registration、read interest 與 callback sequencing 用來交叉檢查實作策略，但不得取代 Servlet specification。

Nginx 1.30.4 是 native request-body buffering / non-blocking flow reference；其 `ngx_http_do_read_client_request_body()`／unbuffered request-body path 用來檢查 bounded buffer、backpressure 與 read-event re-entry 的工程模式，但不得被當成 Servlet semantic authority。

Linux kernel / documented platform APIs 只定義 platform primitive contract。io_uring completion、eventfd 或 epoll readiness 是 native event mechanisms，不直接等同於 Servlet `ReadListener` callbacks。

學術來源只作架構與效能研究證據，不自動成為 Ckarta API contract。SEDA 的 explicit stage/queue 與 overload control 可作 bounded C→Java handoff 的設計參考；實際 contract 仍由本文件及上位規格決定。

## 2. Bridge topology

```text
native connection owner
→ connection-owned request-body FIFO/source
→ native readiness / terminal event
→ ServletInputStream bridge
→ Servlet application / ReadListener
```

C event-loop / native I/O worker 不執行 Servlet application code。

`BodySource` 是 container-internal contract。native pointer、socket、connection registry handle 與 queue implementation details 不得暴露給 Servlet application。

## 3. Source contract

一個 production `BodySource` 必須具有單一明確 owner，且其 lifetime 至少覆蓋所有仍可能執行的 Java read/callback operation。

Source MUST distinguish：

- `DATA`：至少一個 byte 可被此次 read 消費；
- `WOULD_BLOCK`：目前沒有足夠可立即提供的資料，caller 應等待 readiness；
- `EOF`：request body 已完成且沒有更多 body byte；
- `ERROR`：body 已進入 terminal error。

`DATA` 的 payload 必須有 bounded length，且不能讓 Java 看到已失效的 native memory。

Source MUST NOT partially consume a native body span when downstream backpressure rejects that span。若採用 existing C FIFO，應維持目前 all-or-nothing write contract 與 transactional parser acknowledgement。

Source close/cancellation 必須是 idempotent，且必須阻止任何新的 native-to-Java data publication。既有 Java operation 如果已取得 owner pin，pin lifetime 必須覆蓋該 operation 完成；terminal teardown 後不得再 dereference owner-owned memory。

## 4. ServletInputStream blocking semantics

在 blocking mode：

- `read()` / `read(byte[],off,len)` 可以等待 source 提供資料、EOF 或 error；
- `read(ByteBuffer)` 必須遵守 Servlet 6.1 API 的 buffer semantics；若 buffer 無 remaining space，立即回傳 `0` 且不修改 buffer；成功讀取時 buffer position 保持原值，limit 設為原 position 加讀取 byte 數；
- `readLine()`、`readAllBytes()`、`readNBytes()` 等 blocking-oriented operations 不得在 non-blocking mode 執行。

## 5. Servlet non-blocking state

`setReadListener()`：

1. listener 不得為 null；
2. request 必須已進入 Servlet asynchronous processing / upgrade 所允許的 non-blocking context；
3. 一個 input stream 只接受一次 listener registration；
4. listener registration 後，container 負責依 source state 安排第一次 callback；
5. `onDataAvailable()` 後不得同時對同一 listener 進行另一個 concurrent callback。

`isReady()` 的 observable result 與 native source state 必須一致：

- `true`：application 可立即進行下一次 non-blocking read；
- `false`：application 不得繼續 read，直到 container 再次通知 data available。

若 `read()`、`read(byte[],...)` 或 `read(ByteBuffer)` 在 non-blocking mode 被呼叫，而事前 `isReady()` 未回傳 `true`，且不在合法的 `onDataAvailable()` read window，必須丟出 `IllegalStateException`。

## 6. ReadListener sequencing

第一次 `onDataAvailable()`：當 source 已可供讀取時由 container 安排。

後續 `onDataAvailable()`：只有在前一次 read window 中 application 呼叫 `isReady()` 並取得 `false`，之後 source 再次變為 ready 時才能再次通知。

`onAllDataRead()`：只在 request body 已達 EOF，且沒有 terminal error 取代該完成狀態時通知；同一 stream 最多通知一次。

`onError()`：terminal error 確立後通知；第一個 terminal error 是 error authority，其後的 duplicate/late error 不得覆蓋它。

所有 listener method invocation 必須在 container-defined synchronization/serialization boundary 下執行，使同一 listener 不會因不同 event source 而產生未定義的 concurrent callback。

## 7. Backpressure and readiness

native body FIFO 的有界容量不是 Servlet API 本身的 semantics，但它提供 transport-to-application backpressure 的實作基礎。

當 body FIFO 無法接受下一個完整 pending body span 時：

```text
native parser
→ keep pending span
→ report body backpressure
→ wait for consumer progress
→ retry same span
```

不得以重新 feed 已拒絕的 body prefix 來恢復進度，因為這可能重播 parser state。

Servlet `isReady() == false` 應由 native source 的「暫時不可讀」狀態導出，而不是直接把「Java executor queue full」當成 socket readiness；兩者可以同時相關，但 authority 不同。

Nginx 的 request-body read path 顯示 bounded request-body buffers 與 `NGX_AGAIN`/read-event re-entry 是合理的 native control pattern；Ckarta 採同一類型的 backpressure discipline，但保留自身 parser/ownership contract。

## 8. Lifecycle and cancellation

request body source 的 lifecycle 至少包括：

```text
CREATED
→ ACTIVE
→ EOF
or
→ ERROR
or
→ CANCELLED
→ CLOSED
```

`EOF`、`ERROR`、`CANCELLED` 與 `CLOSED` 不得混成單一 observable state：

- EOF 表示 body semantic completion；
- ERROR 表示 body operation failure；
- CANCELLED 表示 owner/async lifecycle 主動終止；
- CLOSED 表示 resource 已不再可使用。

AsyncContext terminal arbitration 與 native connection terminal arbitration 必須使用現有 request id / owner token / lifetime token / cycle identity contract；不得因 `ReadListener` callback arrival order 推測 owner 是否仍有效。

## 9. Native notification

第一階段不得要求 `ServletInputStream` 知道 epoll、eventfd 或 io_uring 的細節。

native backend 只需提供 source-level readiness transition：

```text
NOT_READY
→ READY
→ CONSUME
→ NOT_READY or EOF
```

若 Linux backend 使用 eventfd，eventfd notification 只表示「應再次檢查 completion/readiness state」，不得把 notification 次數直接解釋成 body byte 數或 completion 數。

若未來使用 io_uring，CQE/completion memory ordering、lifetime 與 cancellation 必須依 Linux UAPI/kernel contract separately verified；不得把 io_uring capability 或 completion event 直接提升為 Servlet semantic event。

## 10. Current implementation boundary

目前 `CkartaServletInputStream` 是 minimum Java semantic adapter，`BodySource` 仍為 container-internal abstraction。

目前尚未完成：

- native `ck_http_request_body_t` → production `BodySource` binding；
- native readiness notification → Java callback bridge；
- request adapter `getInputStream()` production exposure；
- AsyncContext ↔ connection cancellation integration；
- owner pin / lifetime extension for async Java reads；
- production multi-worker routing；
- Servlet 6.1 TCK verification。

因此任何 implementation 或 documentation 不得把目前 Java adapter 描述成完整 Servlet non-blocking request-body implementation。

## 11. Reference sources

Jakarta Servlet 6.1:
https://jakarta.ee/specifications/servlet/6.1/
https://jakarta.ee/specifications/servlet/6.1/apidocs/jakarta.servlet/jakarta/servlet/servletinputstream

Tomcat 11.0.25 reference revision is fixed in `docs/REFERENCE_SOURCES.md`; relevant implementation source:
https://github.com/apache/tomcat/blob/main/java/org/apache/coyote/Request.java

Nginx 1.30.4 reference revision is fixed in `docs/REFERENCE_SOURCES.md`; relevant request-body source:
https://github.com/nginx/nginx/blob/master/src/http/ngx_http_request_body.c

Linux io_uring UAPI:
https://github.com/torvalds/linux/blob/master/include/uapi/linux/io_uring.h

SEDA:
Matt Welsh, David Culler, Eric Brewer, “SEDA: an architecture for well-conditioned, scalable internet services”, ACM SIGOPS Operating Systems Review 35(5), 230–243 (2001), DOI 10.1145/502059.502057.
https://doi.org/10.1145/502059.502057
