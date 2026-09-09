# Ckarta Error × Lifecycle × Owner × HTTP Outcome Matrix

本文件是 exception handling 與 request/completion state 對齊的唯一矩陣基線。

`FAILING` 是 C request 的 private error-publication state；它不對外表示 terminal outcome。它不取代 `docs/EXCEPTION_HANDLING_RESEARCH.md` 的長篇研究；本文件只固定 state transition、owner、error classification 與 protocol outcome 的對照。

## 1. 為什麼需要 error record

目前 `ck_request_state_t` 包含 PENDING、RUNNING、CANCELLING、FAILING、COMPLETED、FAILED；其中 FAILING 是 private publication state，不是對外 terminal outcome；單一 FAILED 無法保留 failure source。Java completion 目前只帶 `status`，也不足以區分 Java application exception、executor rejection、JNI failure、timeout 或 upstream/resource failure。

因此正式 request/completion boundary 需要一個小型、process-local、固定布局的 `ck_error_t`。它承載 stable category/code 與有限控制資訊；不保存 Java Throwable、stack trace、動態診斷字串或 owning heap pointer。

## 2. Lifecycle matrix

| Current state | Event | Winner/authority | Next state | Error category | Default HTTP outcome | Cleanup rule |
|---|---|---|---|---|---|---|
| PENDING | begin accepted | request owner | RUNNING | NONE | none | owner retains request |
| PENDING | cancel wins | request owner | CANCELLING | CANCELLATION | no response / close as phase permits | suppress future terminal overwrite |
| PENDING | dispatch rejected | request owner / admission boundary | FAILED | RESOURCE | 503 or local rejection | no Java task was started |
| RUNNING | Java completes normally | request owner via completion | COMPLETED | NONE | application response | release after response ownership ends |
| RUNNING | Java application throws | Java/container then request owner | FAILING → FAILED | APPLICATION | 500 or Servlet-defined error dispatch | only the failure winner writes error record before publishing FAILED |
| RUNNING | JNI operation fails | JNI/native owner | FAILED | JNI | 500 / close depending on phase | pending exception must be handled before further unsafe JNI use |
| RUNNING | timeout wins | request owner | CANCELLING | TIMEOUT | 408/504/close according to phase | cancel Java work; late completion is ignored |
| RUNNING | client disconnect | connection owner | CANCELLING | CANCELLATION | no client response | connection/resource teardown remains owner-controlled |
| RUNNING | invariant violation | server control plane | FAILED or FATAL | INTERNAL/FATAL | fail closed; process shutdown if fatal | stop using corrupted state |
| CANCELLING | completion arrives | request owner | CANCELLING | late completion | no second HTTP outcome | do not free twice |
| CANCELLING | cancel repeats | same owner | CANCELLING | CANCELLATION | unchanged | idempotent |
| COMPLETED | duplicate completion | request owner | COMPLETED | INTERNAL/late duplicate | unchanged | ignore; diagnose if required |
| FAILED | duplicate failure | request owner | FAILED | INTERNAL/late duplicate | unchanged | ignore; diagnose if required |

## 3. Owner matrix

| Resource | Primary owner | Java may borrow? | Error authority |
|---|---|---|---|
| C connection | C connection owner | no direct ownership | connection state machine |
| C request | connection or explicit async owner | Java facade only | request owner |
| native body buffer | C request owner | yes, borrow-only | buffer owner |
| Java Servlet request | Java container | n/a | Java container |
| Java Throwable | Java execution/container | never as C-owned ABI object | Java layer |
| completion record | producer until publication, then consumer/owner | Java creates value record only | completion routing owner |
| JVM lifecycle | C process owner + bootstrap coordination | Java controls container semantics | process lifecycle |

## 4. Error category → HTTP mapping

| Category | Typical source | Client-visible by default | Typical status | Retry default |
|---|---|---|---|---|
| INPUT | malformed HTTP/config/request data | yes, sanitized | 400-class | no |
| POLICY | access/routing/resource policy | yes, sanitized | 403/404/405/429/503 | no unless policy explicitly permits |
| RESOURCE | bounded queue/buffer/thread/connection capacity | yes, sanitized | 429/503 | only when operation is safely replayable |
| TIMEOUT | request/upstream/async deadline | yes, sanitized | 408/504 or close | only if semantics permit |
| CANCELLATION | disconnect/shutdown/async cancellation | usually no response | none/close | no |
| UPSTREAM | proxy/CGI/FastCGI/backend failure | yes, sanitized | 502/504 | only after idempotency/replayability proof |
| APPLICATION | Servlet exception | yes, via Servlet semantics | usually 500 | no |
| JNI | bridge/buffer/reference/invocation failure | minimal 500/close | 500 or close | no default |
| INTERNAL | violated invariant / unexpected implementation state | minimal generic failure | 500/close | no |
| FATAL | process/JVM/native integrity failure | generic failure if possible | connection close / process shutdown | no |

HTTP status must remain a protocol outcome, not the canonical failure identity. A single 503 may correspond to resource exhaustion, policy, maintenance or upstream strategy.

## 5. Exactly-once terminal outcome

Terminal publication is defined as a compare-and-establish operation on request ownership. The first valid terminal winner controls response/completion and cleanup. Later events may be recorded for diagnostics, but cannot perform a second cleanup or change the externally visible outcome.

Formally, for one request owner O:

`terminal_count(O) <= 1`

and after terminal ownership has been established:

`cleanup_count(O) = 1`

provided the object reached a cleanup-eligible state. `CANCELLING` is intentionally non-terminal because a cancellation request may still be followed by resource drain; however, it prevents a later ordinary completion/failure from becoming the request's visible terminal result.

## 6. ck_error_t design

Required fields are deliberately small:

- ABI version
- category
- stable code
- optional HTTP status
- retryability
- client-visible flag
- phase
- request_id

Not included:

- Java Throwable reference
- stack trace
- dynamic heap-owned message
- native pointer
- connection/socket descriptor
- arbitrary vendor-specific payload

The detailed diagnostic context remains in the owning layer and is correlated with request/connection identity.

## 7. Required tests

1. Every lifecycle transition in the matrix.
2. Resource rejection vs application exception classification.
3. JNI pending exception conversion.
4. timeout vs client-disconnect race.
5. cancellation vs completion race.
6. late and duplicate completion.
7. owner teardown before late completion.
8. client-disclosure sanitization.
9. retry suppression for non-idempotent operations.
10. serialization/layout test for the process-local error record.

## 8. Implementation decision

`ck_error_t` is required as a process-local error/outcome record, but it is not a Java exception hierarchy and it is not yet a public plugin ABI. It should first be used internally by C request/completion state. A future externally loadable module ABI, if any, must define a separately versioned compatibility contract.

完整 exception semantics 見 `docs/EXCEPTION_HANDLING_RESEARCH.md`。