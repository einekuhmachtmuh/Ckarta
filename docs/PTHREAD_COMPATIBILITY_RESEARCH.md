# Ckarta pthread 相容性與效能研究

本文件是 Ckarta 對 POSIX pthread／Linux NPTL 使用方式的 canonical research document。它描述 API contract、Linux 實作、thread lifetime、同步原語、CPU／cache／scheduler 關係，以及對 Ckarta 架構的工程限制；不把 Linux/glibc 的實作細節誤當成 POSIX 規格，也不把研究結論直接當成目前實作狀態。

## 1. 研究結論

### 1.1 pthread 不是「輕量 coroutine」

在 Linux/glibc 的 NPTL 實作中，`pthread_create()` 建立的是與 Linux scheduler 整合的 native thread。glibc 的 NPTL thread descriptor、TLS、thread stack、thread exit/join state 與 kernel clone/futex 機制共同構成實作；因此 Ckarta 不應把 pthread 視為只在 user space 排程的 coroutine，也不能假定建立／切換 pthread 的成本接近一般 function call。

POSIX 本身只規範 pthread API 的 observable semantics，不規定所有系統都採 Linux NPTL 的實作。因此 portability contract 必須區分「POSIX API 語意」與「Linux reference implementation 行為」。

### 1.2 Linux pthread 的核心不是「每次操作都 syscall」

NPTL 的同步原語採 user-space fast path + kernel-assisted blocking/wakeup。glibc 的 futex abstraction 用 atomic state 與 futex wait/wake 把 uncontended synchronization 留在 user space；真正需要睡眠／喚醒時才進入 kernel。這也是為什麼 pthread mutex/condition variable 不能簡化成「每次 lock 都 syscall」來估算成本。

glibc 的 NPTL source history 明確顯示 mutex、condition variable、join 等路徑以 futex-internal abstraction 實作；`pthread_join()` 的等待亦利用 thread state 與 futex，並與 Linux `CLONE_CHILD_CLEARTID` 的 thread termination notification 整合。

### 1.3 thread creation 涉及 stack/TLS/lifetime，不是單純分配一個 ID

`pthread_create()` 的實際生命週期包含 thread descriptor、TLS、stack/guard region、thread start wrapper、signal state、thread-local runtime state，以及 exit/join/detach resource reclamation。glibc 亦會管理 pthread stack cache；因此大量短生命週期 pthread 會造成 memory footprint、cache/TLB pressure、scheduler bookkeeping 與 lifecycle synchronization 成本。

### 1.4 pthread 的效能高度取決於工作是否真正平行

多執行緒並不自動提高效能。若 threads 因 shared mutable state 而頻繁競爭 mutex/atomic，或工作量太細而同步、wake-up、cache coherence 與 scheduling overhead 佔主導，增加 thread 數量可能降低 throughput 並增加 tail latency。

Linux scheduler 會依 runnable task、CPU capacity、priority 與 scheduling class 等資訊安排 execution。現代 Linux 已由舊 CFS 模型逐步轉向 EEVDF，因此不應在 Ckarta 文件中把固定「time slice」模型當成今日 Linux scheduler 的完整描述。

### 1.5 CPU cache locality 是 Ckarta 使用 pthread 時的重要設計約束

每個 CPU 有自己的 cache hierarchy。不同 worker thread 若反覆寫入同一 cache line，即使它們存取的是不同欄位，也可能產生 false sharing 與 cache-line ownership transfer。這種 coherence traffic 可能使 atomic/mutex 本身看似很快，但整體 throughput 仍惡化。

因此 Ckarta 的 worker ownership、per-worker connection state、per-worker queue 與 bounded handoff 原則，不只是程式碼可讀性選擇，也有明確的 microarchitectural rationale。

### 1.6 memory ordering 不能以 pthread 名稱取代 C11 memory model

pthread mutex/condition variable 有自己的 synchronization semantics；C11 atomics 則有明確的 memory ordering。兩者不可混用成「pthread 已經保證所有東西可見」的模糊模型。

任何 Ckarta shared-state change 都必須先說明 publication、ownership、happens-before 與 reclamation，再決定 mutex、condition variable、atomic 或其他 primitive。`volatile` 不能取代 synchronization。

## 2. Linux/glibc NPTL 實作證據

### 2.1 pthread_create

glibc NPTL `pthread_create.c` 目前會整合 `clone_internal.h`、`futex-internal.h` 與 architecture-specific thread initialization。這反映 thread creation 並非只建立一個 POSIX abstraction；它必須把 userspace thread descriptor、TLS、stack、kernel thread creation 與 architecture runtime state 接起來。

2024 年 glibc source change 將 `__nptl_arch_thread_init` 接入 `pthread_create.c`，而 thread start path 同時依賴 futex/thread internals。

來源：
https://sourceware.org/pipermail/glibc-cvs/2024q2/085352.html

### 2.2 thread stack

glibc 會為 pthread 建立並管理 thread stack；近期 NPTL source 甚至為 glibc-created thread stack 加入 `/proc/self/maps` 的 anonymous VMA naming。這再次證明 pthread stack 是實際 memory resource，而非抽象的「免費執行上下文」。

來源：
https://sourceware.org/pipermail/glibc-cvs/2023q4/083429.html

### 2.3 pthread_join 與 termination

glibc NPTL 的 `pthread_join()` 使用 thread state 與 futex wait。Linux kernel 在使用 `CLONE_CHILD_CLEARTID` 的 thread termination path 會清除指定 memory 並觸發 futex wake-up；glibc 以此完成 join synchronization。

glibc 也曾修正以 thread state 取代不安全的多欄位 termination synchronization，以避免 `pthread_join()`、`pthread_detach()`、`pthread_exit()` 與 normal thread exit 之間的 lifetime race。這是 Ckarta 必須重視「thread handle lifetime 不等於 thread object lifetime」的直接實作證據。

來源：
https://sourceware.org/pipermail/glibc-cvs/2021q3/074081.html
https://sourceware.org/pipermail/glibc-cvs/2021q3/074896.html

### 2.4 mutex/condition variable 與 futex

glibc NPTL 的 mutex/condition variable 路徑使用 futex abstraction；futex 的設計本身就是 user-space atomic state 加上 kernel blocking/wakeup 的混合模型。Linux kernel 官方文件也明確指出 futex 是提供 userspace synchronization primitives 的 syscall mechanism，glibc 會用它實作較高階的 pthread primitives。

來源：
https://sourceware.org/pipermail/glibc-cvs/2020q4/071070.html
https://sourceware.org/pipermail/glibc-cvs/2020q4/071072.html
https://kernel.org/doc/html/latest/userspace-api/futex2.html

## 3. Linux scheduler 與現代 CPU

### 3.1 scheduler

Linux scheduler 不是 pthread library 本身；pthread thread 成為 scheduler 可執行的 task 後，CPU placement、preemption、load balancing 與 scheduling class 由 kernel scheduler 處理。

目前 Linux scheduler 文件說明 EEVDF 已自 Linux 6.6 開始逐步取代舊 CFS 作為 fair scheduling 的主要方向。CFS/EEVDF 的共同核心是以 runnable work、virtual runtime/lag 與 deadline 等資訊分配 CPU，而不是「pthread 每個固定取得一段固定時間片」的簡化模型。

來源：
https://docs.kernel.org/scheduler/sched-design-CFS.html
https://cdn.kernel.org/doc/html/latest/scheduler/sched-eevdf.html

### 3.2 CPU capacity 與 heterogeneous hardware

現代 Linux scheduler 會考慮 CPU capacity；在 capacity-aware scheduling 下，task utilization 與 CPU capacity 會影響 wake-up CPU selection。這對大小核、異質 CPU 與 power/performance scaling 尤其重要。

來源：
https://cdn.kernel.org/doc/html/latest/scheduler/sched-capacity.html

### 3.3 cache locality

pthread worker 若固定處理自己的 connection/request state，可以減少跨 CPU 的 shared mutable state；反之，如果多個 threads 不斷修改同一 registry、queue metadata 或 cache line，CPU coherence traffic 與 cache miss 可能抵消增加 parallelism 的收益。

這也是 Ckarta 現行 worker ownership + bounded handoff 設計應保留的 microarchitectural rationale；但「哪一種 layout 最快」仍必須以 Ckarta benchmark 證明，不能從理論直接宣稱數值收益。

## 4. pthread 與 event-driven server 的比較

### pthread/thread-based

優點：

- programming model 直接；blocking API 容易表達。
- thread-local state 可自然承載 request-local execution context。
- 適合真正需要長時間 CPU 或 blocking work 的 execution stage。

成本與風險：

- thread stack、TLS、scheduler runnable state 與 lifecycle 都是實際資源。
- shared mutable state 需要 synchronization。
- mutex/atomic contention、cache coherence、context switching、wake-up 與 scheduler pressure 可能放大 tail latency。
- thread count 過高會造成 oversubscription，而不是線性增加 throughput。

### event-driven

優點：

- network I/O 可在少量 worker 上 multiplex。
- connection ownership 可降低 shared mutable state。
- bounded queue 可形成明確 backpressure。

成本與風險：

- callback/state-machine 複雜度較高。
- CPU-bound 或 blocking work 必須移出 event loop。
- 多 CPU scaling 需要明確 worker partitioning/ownership，而不能把單一 event loop 當成天然可平行化。

學術研究顯示兩者都可以成為高併發 server 的有效模型：Capriccio 研究 thread-based server 的 scalability；SEDA 研究 explicit stages/queues/resource control；Zeldovich 等人則展示 event-driven 程式可透過 coarse-grained parallelism 使用多 CPU。這些研究不能直接決定 Ckarta topology，最終仍需 Ckarta workload benchmark。

來源：
https://doi.org/10.1145/945469.945471
https://doi.org/10.1145/502059.502057
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

## 5. 與 Nginx/Tomcat 的交叉比對

### Nginx

固定 reference 1.30.4 的設計重點不是大量 pthread worker，而是 master/worker process topology 與每 worker 的 event-driven connection processing。Nginx source 的 event abstraction 同時保留 read/write readiness、EOF、error、posted/completion 等狀態，顯示 event backend 與 worker execution boundary 是分開的。

來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/ngx_event.h
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/unix/ngx_process_cycle.c

### Tomcat

Tomcat 11.0.25 的 endpoint architecture 將 endpoint、poller/acceptor、socket processing 與 executor 分層；Servlet application execution 並不等於 native socket readiness thread。Tomcat 亦提供 executor/thread renewal 等 lifecycle controls，以避免 web application ThreadLocal 狀態跨 application lifecycle 洩漏。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/StandardContext.java

## 6. 對 Ckarta 的相容性結論

### 6.1 不需要全面移除 pthread

目前 main tree 的 C source code search 沒有找到 `pthread_` API 使用；目前 build flag 的 `-pthread` 是 compiler/linker/thread-runtime contract，不等於程式碼一定直接呼叫 pthread API。因此不能因舊程式曾使用 pthread，就機械式移除 `-pthread` 或全面禁止 pthread。

### 6.2 應禁止的是「未經模型化的 pthread 使用」

未來若重新引入 pthread，必須明確記錄：

1. thread role。
2. owner/lifetime。
3. join/detach policy。
4. stack/resource expectation。
5. CPU affinity 或 scheduler policy（若有）。
6. shared state 與 synchronization mechanism。
7. blocking boundary。
8. JNI attachment/detachment（若需要 JVM）。
9. shutdown ordering。
10. benchmark evidence，尤其是 contention、CPU utilization、tail latency、memory footprint。

### 6.3 C event-loop worker 不應被 pthread API 語意綁死

Ckarta architecture 應抽象「worker」與「platform thread implementation」兩層。Linux 可以用 pthread 建立 worker；Windows 或未來其他 platform 可以使用對應 native thread primitive。worker ownership、event-loop semantics、JNI boundary 與 shutdown contract 必須保持 platform-neutral。

### 6.4 不應把 pthread mutex 當作 hot-path 預設

mutex 不是錯誤，也不是禁止使用；但 connection hot path 應先採 worker ownership、single-owner mutation、bounded handoff。只有在共享狀態確實需要時才引入 mutex/condition variable；引入後必須分析 contention、cache locality、priority inversion/blocking 與 shutdown interaction。

## 7. 建議的 repository policy

本研究不支持修改 Ckarta 的 ISO C11 baseline，也不支持禁止 pthread。

應補充的規則只有一個層級：

- pthread/POSIX thread API 是 platform/OS contract，不是 ISO C11 facility。
- 使用 pthread 不得被視為 portable core 的語言功能。
- thread topology 必須由 `docs/THREAD_MODEL.md` 定義 role/ownership/lifetime；研究文件只保存 pthread implementation/performance evidence。
- pthread primitive 的選擇不得以「通常比較快」作為理由；必須能說明 synchronization semantics，並在需要性能主張時提供 Ckarta benchmark evidence。
- `-pthread` build flag 不應被誤解為「必須使用 pthread API」或「pthread 與所有平台等價」。

這些是對現有 WORKING_RULES concurrency/platform API 規則的精確化，不是建立第二套工作守則。

## 8. 對下一階段的影響

目前沒有證據支持因 pthread 本身而重寫 Ckarta thread architecture。相反地，研究支持保留目前 worker ownership + event loop + Java executor boundary，並將 thread creation/synchronization implementation 與 worker model 分離。

前一輪審查發現的明確工程問題仍然優先：HTTP/connection/output 層直接使用 POSIX socket API，尚未完全收斂到 platform socket backend。完成 socket abstraction 後，才進入 `ServletInputStream.BodySource` 與 production native request-body owner 的正式整合。

本文件不把上述下一步視為已完成狀態；驗證狀態仍由 `docs/WORK_STATE.md` 與 Git/CI 決定。
