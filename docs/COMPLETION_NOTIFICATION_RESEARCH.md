# Ckarta Completion Notification 研究

## 1. 目標

把已經完成的多請求 completion record：

C request
→ Java executor
→ Java completion queue
→ C request owner

升級成真正適合 C event loop 的 notification（通知）模型，而不是每個 completion 都由 C 呼叫 JNI poll。

## 2. Linux 候選

Linux epoll 等待 I/O readiness；eventfd 提供 file-descriptor-based event notification，可由 epoll 等待。

候選：

Java completion
→ JNI native notify
→ eventfd write
→ epoll wakeup
→ drain completion queue
→ request owner routing

來源：
https://man7.org/linux/man-pages/man7/epoll.7.html
https://www.man7.org/linux/man-pages/dir_section_2.html

優點：
- 直接整合既有 epoll。
- notification 與 completion queue 可分離。
- 不需要週期性 JNI poll。

代價：
- 每次通知仍可能有 JNI crossing。
- eventfd counter 與 completion queue 必須處理 overflow／coalescing。
- Java completion 與 C wakeup 的 ordering 必須明確。

## 3. Windows 候選

Windows IOCP 本身就是 completion queue。

候選：

Java completion
→ JNI native notify
→ PostQueuedCompletionStatus
→ GetQueuedCompletionStatusEx
→ C worker
→ request owner routing

Microsoft 文件明確指出 PostQueuedCompletionStatus 可將 application-defined completion packet 放入指定 completion port；GetQueuedCompletionStatusEx 可批次取出多個 completion packet。

來源：
https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports
https://learn.microsoft.com/en-us/windows/win32/fileio/getqueuedcompletionstatusex-func

優點：
- 與 Windows native completion model 一致。
- 不需要為了 Java completion 另外建立 pipe。
- 可與 socket I/O completion 共用一個 port。

代價：
- Ckarta 必須接受 Windows completion semantics 與 Linux readiness semantics 不同。
- shutdown／sentinel packet／late completion 必須明確定義。
- completion key／OVERLAPPED identity 必須與 Ckarta owner identity 分離。

## 4. Linux 與 Windows 不應強行共享底層 primitive

共同介面應只有：

platform_event_wakeup
platform_event_wait
platform_event_drain

而不是：

epoll-like API
或
IOCP-like API

Ckarta upper layer 只知道：

EVENT_IO
EVENT_WAKEUP
EVENT_TIMER
EVENT_COMPLETION
EVENT_ERROR
EVENT_CLOSED

## 5. thread／memory ordering

Java executor thread 發布 completion record 後，必須先完成 record 的所有寫入，再觸發 platform wakeup。

C worker 收到 wakeup 後才能讀取 record。

因此至少需要：

completion publication
→ release ordering
→ wakeup publication
→ acquire/consume ordering

實際 C/C++ memory model implementation 必須在正式程式碼中逐欄驗證，不能只以「event 已送出」推定所有資料可見。

## 6. notification coalescing

不一定每個 completion 都需要一次 OS wakeup。

例如：

N completions
→ one eventfd increment／one IOCP packet
→ drain all available completions

但若使用 coalescing，必須保證：

queue 非空
→ 至少一次 wakeup
→ worker 最終 drain queue

不能因競合而出現 lost wakeup。

## 7. backpressure

Java completion queue 滿時不能：

- block C event loop
- 無限配置
- silently drop completion

應有明確：

QUEUE_FULL
→ request failure／backpressure policy

其策略必須與 Java executor queue、C request limit 及 connection lifetime 一致。

SEDA 研究可作為 queue／stage 分離的理論基礎。

來源：
https://doi.org/10.1145/502059.502057

## 8. 學術證據

Joubert 等研究 Linux／Windows user-space Web server 的 event notification、data movement 與 communication path，顯示 OS primitive 本身會影響高效能 server。

https://www.usenix.org/conference/2001-usenix-annual-technical-conference/high-performance-memory-based-web

Hu、Pyarali、Schmidt 的 Windows NT Web server 研究指出 asynchronous I/O 與不同 dispatch model 的效能會隨 load／file size 改變。

https://www.usenix.org/legacy/publications/library/proceedings/usenix-nt97/usage_abstracts/James_Hu.html

因此 Ckarta 應採平台原生通知，而不是假定一種 primitive 在兩平台均最佳。

## 9. 最終候選

Linux：
epoll + eventfd + bounded completion queue

Windows：
IOCP + PostQueuedCompletionStatus + bounded completion queue

尚未實作。

正式 benchmark 必須比較：

- notification latency
- syscalls per completion
- batches per wakeup
- p50/p95/p99
- CPU
- cross-thread contention
- lost/duplicate wakeup tests
- shutdown／cancellation correctness


## 8. Current executable decision

本階段已選擇 Linux `eventfd(EFD_CLOEXEC | EFD_NONBLOCK)` 作為第一個可執行 notification backend prototype；completion records 仍由獨立 bounded mutex-protected ring queue 保存。`eventfd` 只負責 wake-up/count，不攜帶 request identity。queue producer 先在同一 critical section 發布 record，再執行 nonblocking notification；若 notification 明確失敗，record 會 rollback，避免資料與通知出現半成功狀態。這是 Linux executable backend，不是跨平台最終選型，也尚未接入 Java JNI producer。

研究基線：Linux `eventfd(2)` 官方文件說明 eventfd 建立可供 user-space 使用的 file descriptor event notification 機制，並支援 `poll`/`epoll`；EFD_NONBLOCK 使 notification 操作不需阻塞 event loop。來源：https://man7.org/linux/man-pages/man2/eventfd.2.html

SEDA 的 explicit queue + load conditioning 與本設計的 bounded completion queue 相符，但不代表 Ckarta 必須採用 SEDA 全部架構。來源：https://doi.org/10.1145/502059.502057

Michael/Scott 1996 的 non-blocking queue work 仍僅作替代設計參考；本階段選 mutex-protected bounded queue，因為 ownership、reclamation、shutdown drain 與 overflow 語意比 lock-free micro-optimization 更優先。來源：https://doi.org/10.1145/248052.248106