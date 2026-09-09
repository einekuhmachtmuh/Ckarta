# Ckarta Linux io_uring Backend Research

## 1. Research scope

本文件研究以 Linux io_uring 取代目前 Ckarta Linux epoll event backend 的可行性。核心 constraint 是不引入 liburing；若實作，直接使用 Linux UAPI headers 與 documented system calls `io_uring_setup`、`io_uring_enter`、`io_uring_register`，並保留 platform/backend boundary。

目前結論不是立即取代 epoll。第一階段應建立可選的 Linux io_uring backend，與既有 epoll backend 共用 Ckarta semantic event/connection contracts；runtime 以實際 feature/opcode probe 決定是否啟用，不能只依 `uname()` 的 kernel version string。

## 2. Why io_uring is materially different

epoll is readiness notification: Ckarta receives an indication that a file descriptor can make progress, then the owner calls `recv()`/`send()`.

io_uring is completion-oriented: the owner submits an I/O request with an SQE and later consumes a CQE containing the completion result. The kernel/user-space shared rings also allow batching of submissions and completions.

Therefore a complete replacement cannot simply rename `CK_EVENT_READ` / `CK_EVENT_WRITE` to io_uring calls. The backend contract needs an operation/completion representation containing at least:

- opaque connection cookie;
- operation kind (accept/recv/send/poll/timeout/etc.);
- completion result (`res`);
- completion flags;
- provided-buffer identity when the receive path uses a buffer ring.

The connection owner remains the authority for ordering, lifetime, and terminal state.

## 3. Direct system-call implementation without liburing

The kernel interface consists of:

- `io_uring_setup()`
- `io_uring_enter()`
- `io_uring_register()`

Ckarta should call these through the libc `syscall()` wrapper using the platform-provided `SYS_io_uring_*` constants and `<linux/io_uring.h>`. Raw numeric syscall numbers MUST NOT be hard-coded.

This is compatible with the project rule that platform-specific code may use documented Linux system APIs while portable/core code remains platform-neutral.

The implementation must not copy liburing's internal source as a dependency or expose liburing types in Ckarta ABI.

## 4. Kernel-version capability ladder

Kernel version is an initial deployment constraint, not a sufficient runtime feature test.

| Capability | Upstream availability | Ckarta relevance |
|---|---:|---|
| io_uring core | 5.1 | minimum API existence |
| `IORING_OP_ACCEPT` | 5.5 | listener path |
| `IORING_OP_SEND` / `IORING_OP_RECV` | 5.6 | basic socket I/O |
| `IORING_FEAT_FAST_POLL` | 5.7 | efficient socket readiness waiting |
| multishot poll | 5.13 | fewer re-submissions for pure readiness use |
| multishot accept | 5.19 | scalable listener submission |
| provided-buffer ring | 5.19 | bounded receive-buffer ownership |
| multishot recv | 6.0 | high-throughput socket receive path |
| send zero-copy | 6.0 | optional output optimization |
| sendmsg zero-copy | 6.1 | optional output optimization |
| incremental buffer ring | 6.12 | useful for streaming/payload segmentation |
| recv/send bundle | 6.10 | optional batching optimization |
| zero-copy receive (ZCRX) | 6.15 | advanced NIC-dependent optimization; not initial target |
| `IORING_OP_EPOLL_WAIT` | 6.15 | wraps epoll; does not remove epoll dependency |
| io_uring-native bind/listen | 6.11 | optional; initial listener can retain normal socket/bind/listen syscalls |

Sources:
- Linux kernel io_uring documentation/man pages:
  https://man7.org/linux/man-pages/man7/io_uring.7.html
  https://man7.org/linux/man-pages/man2/io_uring_setup.2.html
  https://man7.org/linux/man-pages/man2/io_uring_enter.2.html
- Active kernel releases:
  https://www.kernel.org/releases.html

## 5. Practical support policy

Do not make the product depend on one exact `uname -r` value. Distro kernels may backport features and sandboxing/security policy can reject io_uring even when the nominal kernel version is new enough.

Recommended deployment tiers:

### Tier A: modern preferred

Linux 6.12+.

This supplies multishot recv, modern provided-buffer-ring features, and recent io_uring evolution while still having the 6.12 long-term-support line through Dec 2028.

### Tier B: compatible io_uring

Linux 5.7+ with the required operations/features successfully probed.

Basic network operations exist earlier, but 5.7 is a practical lower boundary for the socket-centric design because `IORING_FEAT_FAST_POLL` first appears there.

### Tier C: legacy fallback

Older supported kernels, or kernels where setup/required opcodes are unavailable/blocked:

use the existing epoll backend.

Ckarta should therefore retain epoll as the compatibility backend until real deployment requirements justify removing it.

As of September 2026, kernel.org lists 6.18 and 6.12 as long-term kernels with projected EOL in December 2028; 6.6 and 6.1 are projected through December 2027, while 5.15 and 5.10 reach projected EOL in December 2026.

## 6. Runtime detection

A probe should:

1. call `io_uring_setup()`;
2. inspect `params.features`;
3. query opcode support with `IORING_REGISTER_PROBE`;
4. explicitly test support for the operations Ckarta actually needs;
5. record setup/probe errno when unavailable.

Do not infer availability from a successful compile or from kernel version alone.

In particular, `EPERM`, `EACCES`, `ENOSYS`, and `EOPNOTSUPP` must be represented as a backend-unavailable condition so the caller can fall back to epoll.

## 7. Initial Ckarta io_uring architecture

Do NOT initially use SQPOLL, IOPOLL, ZCRX, or io_uring's epoll-wrapping opcode.

Initial network backend should use:

- one io_uring instance owned by one C event-loop worker;
- normal `socket()/bind()/listen()` for listener setup;
- one in-flight accept operation, preferably multishot accept when supported;
- one receive stream per owned connection;
- at most one concurrently in-flight send stream per connection;
- CQE user_data carrying an opaque Ckarta operation/connection token;
- explicit connection-owner serialization.

The last rule is important: network I/O submissions on one TCP socket must not be issued concurrently in ways that violate byte ordering or ownership expectations.

## 8. Receive-buffer ownership

The existing Ckarta reader currently owns a 64 KiB receive buffer. That model cannot simply be passed to a multishot recv operation if the kernel may continue using the buffer after the application starts parser/Java processing.

A better later design is:

io_uring provided-buffer ring
→ buffer-id completion
→ connection-owner parser
→ bounded body FIFO / application consumer
→ buffer returned to kernel

This makes buffer ownership explicit.

The first implementation should nevertheless retain ordinary user buffers and one-shot `IORING_OP_RECV` if that is simpler. Multishot + provided buffers should be introduced only after the transactional HTTP body-delivery contract is proven.

## 9. Send path

The output writer already has a bounded pending-output model.

The io_uring adapter should submit one `IORING_OP_SEND` for the current contiguous pending span. A partial positive CQE result advances the output cursor; a zero/negative result maps to connection error/peer close according to the existing output contract.

Do not submit a second send for the same connection while the previous send can still complete. The connection owner remains responsible for output ordering.

EPOLLOUT itself disappears from the io_uring-native path because writable readiness is represented by send completion or re-submission rather than a permanently armed readiness interest.

## 10. HTTP parser interaction

io_uring does not change HTTP semantics.

The CQE only says how many bytes were received. Ckarta still must preserve:

- header boundary;
- Content-Length accounting;
- chunked decoder state;
- pipelined bytes;
- transactional body acknowledgement;
- request recycle boundary.

For a provided-buffer completion, the parser must finish synchronously while the buffer is owned by the consumer. The buffer cannot be returned to the kernel before bytes needed by the current parser state have been consumed or copied to a bounded owner-controlled queue.

## 11. Completion/wakeup model

The current `ck_event_loop_wait()` API is readiness-oriented. Rather than distorting it, introduce a separate Linux io_uring backend interface with a completion-oriented internal representation, then adapt both backends to a common connection-dispatch contract at the worker layer.

The common layer should receive semantic operations such as:

- ACCEPTED
- READ_COMPLETED
- WRITE_COMPLETED
- TIMEOUT
- WAKEUP
- ERROR
- CLOSED

The Linux backend remains responsible for converting io_uring CQEs into those events.

## 12. io_uring-specific batching

io_uring can batch SQE submission and CQE consumption, which is a potential advantage over per-I/O syscalls. But batching must be bounded by the same fairness rules already used for Ckarta readers/writers.

A first production implementation should define explicit budgets for:

- maximum SQEs submitted in one dispatch;
- maximum CQEs consumed in one dispatch;
- maximum bytes received;
- maximum bytes sent.

No claim that io_uring is faster than epoll may be made until the benchmark controls hardware, kernel, compiler, JDK, workload, keep-alive, request/response sizes, CPU affinity, cache state, and concurrency.

## 13. Why SQPOLL is not the initial default

SQPOLL creates a kernel polling thread and changes CPU/resource accounting. The kernel manual itself warns that this is not an automatic performance win.

Ckarta should initially use interrupt-driven rings with explicit `io_uring_enter()`. SQPOLL can be a separately benchmarked mode later.

## 14. Why IOPOLL and ZCRX are not initial targets

IOPOLL is aimed primarily at polling-capable storage devices and is not the right primitive for normal TCP sockets.

ZCRX can reduce copying but introduces registered zero-copy receive infrastructure and a more complicated memory lifetime. It should not be mixed into the first correctness implementation.

## 15. Security and lifecycle

The io_uring file descriptor is a C-owned process/worker resource. It must not cross the Java ABI.

Every SQE must hold a lifetime-stable user_data identity until its CQE is consumed or cancellation/teardown has been conclusively resolved.

Connection close/retire must prevent submission of new operations and must account for already in-flight SQEs before freeing buffers or connection storage.

The stale-event rule for epoll becomes a stale-completion rule for io_uring: a CQE can arrive after the logical connection state changed, so the consumer must validate generation/correlation identity before touching mutable connection state.

## 16. Kernel configuration and deployment checks

The deployment checklist must include:

- `io_uring_setup()` permitted by security policy;
- required io_uring opcode support;
- required feature flags;
- memory-accounting limits for registered buffers;
- process/resource limits;
- kernel LTS status;
- container/sandbox policy.

A new kernel is not enough if a container policy denies io_uring.

## 17. Nginx/Tomcat comparison

Nginx 1.30.4 uses an abstract event layer with Linux epoll as one backend. Its core separates event demultiplexing from connection/request processing.

Tomcat 11.0.25 separates Poller readiness, socket wrapper state, processor execution, and application/container processing. The Servlet layer never sees Linux epoll/io_uring details.

Ckarta should preserve the same layering principle: io_uring belongs entirely below connection/HTTP state and below the Java Servlet ABI.

## 18. Research literature

- Jonathan Corbet et al. / Linux kernel community documentation on io_uring, via the Linux man-pages/UAPI sources above.
- Constantin Pestka, Marcus Paradies, Matthias Pohl, “Asynchronous I/O — With Great Power Comes Great Responsibility”, arXiv:2411.16254, 2024. The paper emphasizes that modern asynchronous I/O reduces overhead but introduces substantial architectural complexity and design trade-offs.
  https://arxiv.org/abs/2411.16254
- Matthias Jasny, Muhammad El-Hindi, Tobias Ziegler, Viktor Leis, Carsten Binnig, “High-Performance DBMSs with io_uring: When and How to Use It”, PVLDB 19(9), 2026. The authors report that replacing traditional I/O with io_uring is workload- and architecture-dependent and show how registered buffers and other advanced facilities can affect end-to-end results.
  https://vldb.org/2026/program.html
- Matt Welsh, David Culler, Eric Brewer, “SEDA: An Architecture for Well-Conditioned, Scalable Internet Services”, ACM SIGOPS OSR 35(5), 2001, DOI 10.1145/502059.502057.
- Leslie Lamport, “Concurrent Reading and Writing”, Communications of the ACM 20(11), 806–811, 1977, DOI 10.1145/359863.359878.

The academic sources support architectural reasoning and trade-off analysis; none proves that Ckarta's io_uring implementation is correct or faster.

## 19. Decision

io_uring is technically feasible for Ckarta without liburing.

It is NOT yet justified to delete epoll.

Recommended implementation strategy:

1. retain epoll as compatibility backend;
2. add Linux io_uring probe;
3. add a separate completion-oriented Linux backend;
4. start with one-shot ACCEPT/RECV/SEND and direct system calls;
5. prove lifetime, cancellation, and stale-completion correctness;
6. then add multishot accept/recv and provided-buffer ring on kernels where available;
7. benchmark epoll vs io_uring under identical controlled workloads;
8. only then decide default backend and minimum supported kernel.

The preferred modern target is Linux 6.12+; the compatibility floor for the first socket-centric io_uring backend is 5.7+ when runtime probing confirms the required features. Older/unavailable/blocked environments fall back to epoll.

No liburing dependency is required by this design.
