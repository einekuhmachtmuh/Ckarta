# Ckarta Canonical Request Handoff

## Scope

本文件定義目前已實作並測試的第一個 C HTTP → JNI → Java request handoff slice。它不是完整 ServletRequest implementation，也不改變 C canonical request 作為 request authority 的定位。

## Current path

C HTTP parser
→ `ck_request_init_http()`
→ process-local `ck_request_descriptor_t`
→ JNI `dispatchAsync()`
→ Java executor
→ `NativeRequest`
→ native completion record

## Metadata view

metadata buffer is bounded and contains:

`u32 method_length | u32 target_length | u32 protocol_length | method | target | protocol | u8 connection_close_required`

All length integers are big-endian. Java receives a read-only DirectByteBuffer view and slices it without constructing per-header Java objects.

Current canonical builder copies only method/target/protocol metadata into request-owned storage. It does not copy the complete header object graph.

## Body view

The body remains C-owned and may be exposed to Java only for an explicitly bounded borrow interval. A zero-length body may have a NULL native pointer and is represented on the Java side by an empty direct buffer.

For non-zero body data, the native owner must keep the underlying storage alive until the Java-visible borrow interval and completion/lifecycle handoff have ended. The current request handoff smoke uses bodyless GET requests, so it does not by itself prove production Servlet request-body streaming.

The connection-reader path now has a separate 64 KiB bounded SPSC body FIFO. It uses transactional pending-body acknowledgement and an all-or-nothing enqueue contract, so reader input is not acknowledged until the complete pending body span is accepted by the bounded consumer. This native path is lifetime-safe for staged body delivery but is not yet the production Java `ServletInputStream` owner.

## ABI

The request descriptor version is now 2 because metadata was added. The descriptor remains process-local; Java never receives the C struct or any native pointer.

Current fields include:

- abi_version
- struct_size
- feature_flags
- ownership_flags
- owner_token
- lifetime_token
- request_id
- metadata / metadata_length
- body / body_length

## Java semantics

`NativeRequest` is a thin container-internal facade. It exposes read-only metadata/body views and provides metadata byte slices for method, target, and protocol. It is not the application-facing `HttpServletRequest`.

The current Java smoke path validates:

- method = GET
- protocol = HTTP/1.1
- target is non-empty
- `Connection: close` is not requested

These checks are integration-test semantics, not the final Servlet request implementation.

## Lifecycle rule

The canonical request descriptor must not outlive its native owner. Java may retain only borrowed views whose lifetime is explicitly covered by the C request owner. Completion publication is value-only and does not retain Java Throwable, JNI environment pointers, or raw native pointers.

## Upstream cross-check

Tomcat 11.0.25 separates protocol processing from Catalina request creation: `Http11Processor.service()` parses the HTTP request, prepares it, then calls `CoyoteAdapter.service()`, where the Coyote request/response are connected to Catalina objects and the Container Pipeline is invoked.

Nginx 1.30.4 similarly keeps HTTP parsing/request processing in the C HTTP layer and does not expose its native connection/request structs as an application ABI.

Fixed source versions:

- Nginx 1.30.4: `017cf98dcce217946572a896f0992370475e189f`
- Tomcat 11.0.25: `cbe6e15ee81e2fc6232954292a80cca5d1e84009`

## Remaining gate

The minimal Java `CkartaServletInputStream` / `ReadListener` semantic adapter now exists, but this document still does not represent a complete Servlet request implementation. Remaining work is production integration of the connection-owned body source with the application-facing request surface, including `getInputStream()`, readiness notification, owner pin/lifetime extension, AsyncContext ↔ connection cancellation, and safe request recycle after Java completion.

No Servlet 6.1 compatibility claim is made by this document.

## Body lifetime and native stream boundary

A connection reader buffer that may be compacted or reused for pipelined input MUST NOT be handed to Java as an asynchronously retained DirectByteBuffer. Java-visible request-body streaming instead requires a native owner whose lifetime extends across the Java borrow interval.

Ckarta now provides that native staging primitive in:

- `c/http/ck_http_request_body.h`
- `c/http/ck_http_request_body.c`

The queue has fixed 64 KiB capacity. The connection reader uses a transactional pending-body state: when a body consumer reports backpressure, the pending span remains unacknowledged and the reader buffer offset is not advanced. The queue write is all-or-nothing; when the available space is smaller than the complete pending span it returns `CK_HTTP_REQUEST_BODY_WRITE_WOULD_BLOCK` without moving `head` or writing a partial prefix. Once the complete span is accepted, the reader acknowledges the pending input and may continue framing.

This closes the previously identified parser-replay hazard at the native reader/body boundary. It does not by itself establish the Java owner/lifetime, Servlet readiness callback, request `getInputStream()` integration, or AsyncContext cancellation semantics required for production Servlet streaming.

## Concurrency research basis

The bounded body FIFO is intentionally single-producer/single-consumer. The C event-loop/request-body producer and the Java-facing body consumer must each have a unique owner; the queue is not a general multi-producer/multi-consumer object and is not advertised as thread-safe beyond this contract.

Research basis: Leslie Lamport, "Concurrent Reading and Writing", Communications of the ACM 20(11), 806–811 (1977), DOI 10.1145/359863.359878. The paper studies communication between asynchronous processes under explicit single-writer/single-reader assumptions and gives bounded communication techniques. Ckarta uses this as a concurrency reasoning baseline only; the actual C11 atomic implementation still requires repository tests and the C memory model.

Source:
https://doi.org/10.1145/359863.359878

Servlet 6.1 requires `ServletInputStream.isReady()` to indicate whether a non-blocking read may proceed and uses `ReadListener.onDataAvailable()` / `onAllDataRead()` for asynchronous data availability. These semantics are the reason Ckarta cannot equate a filled native FIFO with completion of the HTTP request; request parsing, body availability, Java read readiness, and message completion remain separate states.

Official source:
https://jakarta.ee/specifications/servlet/6.1/apidocs/jakarta.servlet/jakarta/servlet/servletinputstream
https://jakarta.ee/specifications/servlet/6.1/apidocs/jakarta.servlet/jakarta/servlet/readlistener
