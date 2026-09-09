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

The body remains a C-owned buffer borrowed by Java for the request lifetime. A zero-length body may have a NULL native pointer and is represented on the Java side by an empty direct buffer.

For non-zero body data, the native owner must keep the buffer alive until Java request processing and completion publication have finished. The current handoff smoke uses bodyless GET requests, so it does not yet prove streaming request-body ownership across Java execution.

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

The next step is not a second generic wrapper. It is to create the minimal application-facing Servlet request adapter only after the following are explicit and tested:

1. canonical request metadata lifetime;
2. request body streaming/backpressure semantics;
3. header access semantics;
4. Servlet executor handoff ownership;
5. cancellation and client-disconnect interaction;
6. request recycle after Java completion.

No Servlet 6.1 compatibility claim is made by this document.
