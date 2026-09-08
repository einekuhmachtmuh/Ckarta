# Ckarta 跨平台 completion notification 研究

## 1. 目標

把 Java executor 完成事件以不阻塞 C event loop 的方式通知 native runtime。通知機制只負責 wake-up；真正的 completion record 仍存放在受控 queue／owner state 中。

## 2. Linux 候選

### eventfd
Linux eventfd 是一個用於 event notification（事件通知）的 file descriptor，counter 可透過 read/write 操作，且可由 epoll 監視。其主要優勢是只需一個 descriptor；Linux manual 也明確指出，若 pipe 只用於 signalling，eventfd 可作為替代，kernel overhead 較低。

來源：https://man7.org/linux/man-pages/man2/eventfd.2.html
來源：https://www.man7.org/linux/man-pages/man7/epoll.7.html

### pipe
pipe 是一般性 IPC（程序間通訊）機制，也可放入 epoll；但若資料只代表「有 completion」，其 byte-stream 語意較 eventfd 複雜。

### socketpair
socketpair 可作本機雙向通知，但對同程序的 completion wake-up 語意偏重，除非未來需要雙向控制流，否則暫不優先。

## 3. Windows 候選

### IOCP + PostQueuedCompletionStatus
Windows IOCP 可由 GetQueuedCompletionStatus 等待；PostQueuedCompletionStatus 可主動向指定 completion port 張貼 completion packet，並將 completion key 與 OVERLAPPED context 帶給 consumer。

來源：https://learn.microsoft.com/zh-tw/windows/win32/fileio/i-o-completion-ports
來源：https://learn.microsoft.com/zh-tw/windows/win32/api/ioapiset/nf-ioapiset-postqueuedcompletionstatus
來源：https://learn.microsoft.com/zh-tw/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus

這使 Windows 上的 Java→C completion 與 native network completion 能使用相同 IOCP wait domain。

## 4. Nginx 對照

Nginx 1.30.4 的 ngx_iocp_module.c 將 CreateIoCompletionPort、GetQueuedCompletionStatus 與 Overlapped socket I/O 放在 event module（事件模組）中；其 event abstraction 已將 platform primitive 隔離於 HTTP request path。

固定來源：https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/modules/ngx_iocp_module.c

Nginx Windows 也保留 win32 select module，證明 fallback 可以存在，但不應把較弱的機制當成高並行 Windows 主路徑。

## 5. Apache 對照

Apache httpd 2.4.68 的 mpm_winnt 使用 Windows Event、HANDLE、AcceptEx 與 thread model；它沒有把 Unix signal semantics 硬搬到 Windows，而是將 control-plane notification 平台化。

固定來源：https://github.com/apache/httpd/blob/2.4.68/server/mpm/winnt/mpm_winnt.c

這支持 Ckarta 將 control notification 與 network I/O notification 分開建模，但 Windows network data plane 仍優先 IOCP。

## 6. Tomcat 對照

Tomcat 11.0.25 Nio2Endpoint 使用 AsynchronousChannelGroup、AsynchronousServerSocketChannel、CompletionHandler 等 Java 層非同步抽象。這說明 Windows completion-style I/O 在 JVM 內已有成熟高階表示，但 Ckarta C network ownership 不應因此轉移到 Java。

固定來源：https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/Nio2Endpoint.java

## 7. 統一模型

建議的語意是：

Java executor
→ JNI notification
→ platform completion signal
→ C event backend wake-up
→ drain completion queue
→ request_id / owner_token / lifetime_token routing

Linux：eventfd + epoll。
Windows：PostQueuedCompletionStatus + IOCP。

上層不應知道 eventfd 或 IOCP。

## 8. 性能模型

令 T_n 為 notification 的 wake-up 與 drain 成本，T_q 為 completion queue 操作成本，T_j 為 JNI crossing 成本。

一次 completion 的額外成本至少可表示為：

T_extra = T_j + T_q + T_n

因此不應只 benchmark eventfd 與 IOCP；真正要測的是完整 Java→JNI→notification→C wake-up→queue drain。

若 Java worker 已經需要 JNI crossing 來通知 C，則比較 primitive 時不能忽略 JNI 成本。

高負載下還需量測：
- p50／p95／p99 wake latency
- completions/s
- CPU cycles per completion
- queue contention
- wake batching efficiency
- lost／duplicate notification
- shutdown latency

## 9. overflow 與 coalescing

notification 不必一對一對應 completion。可以採 coalescing（合併通知）：

N completions → one wake-up → drain N records

因此 queue 是 correctness state，notification 是 wake-up hint。

這個區分非常重要：不能因為 notification 重複或合併就認為 completion 遺失；真正的 correctness 必須由 completion queue／owner state 保證。

## 10. Cancellation / shutdown

notification primitive 關閉前，必須先停止新 completion publication，再 drain 已發佈 completion，最後關閉 platform primitive。

任何 late completion 都必須能透過 owner/lifetime token 被拒絕，而不依賴 notification primitive 本身提供生命週期保證。

Windows IOCP 的 completion packet 與 OVERLAPPED context 仍需遵守 native object lifetime；Linux eventfd 只表示 readiness，不保存 Ckarta request identity。

## 11. 學術依據

Matt Welsh、David Culler、Eric Brewer，SEDA: an architecture for well-conditioned, scalable Internet services，ACM SIGOPS Operating Systems Review 35(5), 2001，DOI：https://doi.org/10.1145/502059.502057。

Vivek S. Pai、Peter Druschel、Willy Zwaenepoel，Flash: An Efficient and Portable Web Server，USENIX ATC 1999：https://www.usenix.org/conference/1999-usenix-annual-technical-conference/flash-efficient-and-portable-web-server。

Gaurav Banga、Jeffrey C. Mogul，Scalable Kernel Performance for Internet Servers Under Realistic Loads，USENIX ATC 1998：https://www.usenix.org/conference/1998-usenix-annual-technical-conference/scalable-kernel-performance-internet-servers。

## 12. 決策

Linux primary notification：eventfd + epoll，暫定優先。
Windows primary notification：IOCP + PostQueuedCompletionStatus，暫定優先。
pipe：Linux portability fallback 候選。
socketpair：暫不優先。
Windows Event：控制面候選，不作高頻 request completion 首選。

以上仍需 Ckarta 自有 benchmark 後才能宣稱效能優勢。
