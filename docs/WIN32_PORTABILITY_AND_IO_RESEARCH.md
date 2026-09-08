# Ckarta Win32 支援與底層作業系統介面研究

## 1. 結論

加入 Windows 支援在架構上可行，而且不需要改變 C/Java 核心邊界。

建議將：
- Ckarta protocol／connection／request state machine
- C event abstraction
- JNI ABI
- Java Servlet semantics
維持跨平台；把作業系統差異隔離在 platform backend（平台後端）：

Linux：Ckarta event backend → epoll 等 Linux/Unix 能力。
Windows：Ckarta event backend → IOCP + Overlapped I/O + Winsock2。

Windows 不應以把 epoll API 換成 select() 作為正式高效能路徑；select 可作 fallback（回退）但不應成為 Windows 高負載基線。

## 2. Apache httpd 的 Windows 實作

Apache HTTP Server 2.4.68 在 Windows 使用 mpm_winnt，而 Unix 使用 worker/event 等其他 MPM（多程序模組）。

官方文件指出，Windows 的 mpm_winnt 是預設 MPM，採單一控制程序、單一 child process，再由 child 建立 threads；ThreadsPerChild 控制並行客戶端連線數。

來源：
https://httpd.apache.org/docs/current/mod/mpm_winnt.html
https://httpd.apache.org/docs/current/mpm.html
https://github.com/apache/httpd/blob/2.4.68/server/mpm/winnt/mpm_winnt.c

固定原始碼亦直接使用 Windows HANDLE、CreateEvent、SetEvent、DuplicateHandle、ReadFile 等作為程序／執行緒控制與程序間協作。

Apache 的重要啟示不是 Windows 必須使用 thread-per-connection，而是作業系統不同時，MPM／runtime backend 可以不同，上層 HTTP semantics 不必改變。

Apache mpm_winnt 還使用 AcceptEx；官方文件描述其預設會使用進階的 Windows accept API，發生 AcceptEx faults 時可以 fallback。

## 3. Nginx 的 Windows 實作

固定 Nginx 1.30.4：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/modules/ngx_iocp_module.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/modules/ngx_win32_select_module.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/win32/ngx_socket.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/win32/ngx_wsarecv.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/ngx_event_accept.c

Nginx Windows 有明確 IOCP event module：
ngx_iocp_init() → CreateIoCompletionPort()。

每次完成事件經 GetQueuedCompletionStatus() 取得 bytes、completion key、OVERLAPPED，再映射成 Nginx event handler。

Nginx Windows socket I/O 則經 Overlapped WSARecv／WSASend。

Nginx 也保留 win32 select module，但該 backend 使用 FD_SETSIZE、fd_set 與 descriptor scanning，因此不應作 Ckarta Windows 高負載預設。

## 4. Tomcat 的 Windows 能力

固定 Tomcat 11.0.25 的 Nio2Endpoint：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/Nio2Endpoint.java

它使用 AsynchronousChannelGroup、AsynchronousServerSocketChannel、AsynchronousSocketChannel 與 CompletionHandler。

Nio2Endpoint.bind() 建立非同步 channel group 與 server socket；accept 採非同步 accept operation。

這說明 Java/Tomcat 已提供與 Windows completion-oriented I/O 相容的高階抽象，但 Ckarta 不應把 C network data plane 放進 Java NIO2；native ownership 仍在 C。

## 5. Win32 與 Linux 的本質差異

Linux 常見 readiness model（就緒模型）：
epoll_wait() → socket ready → application read/write。

Windows IOCP 是 completion model（完成模型）：
GetQueuedCompletionStatus() → I/O 已完成 → application 處理 completion。

所以 Ckarta event abstraction 不能只定義「fd 可讀／可寫」。

應定義更一般的語意：
event notification → event kind → opaque operation context → bytes/error/result → owner。

這樣 Linux readiness 與 Windows completion 都可以映射到相同上層 request／connection state machine。

## 6. Windows Overlapped I/O 的生命週期

Microsoft 的 WSARecv 文件明確指出，Overlapped I/O 的 WSAOVERLAPPED 必須在非同步操作整個期間保持有效；多個 simultaneous outstanding operation 必須各自有不同的 WSAOVERLAPPED。

來源：
https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsarecv

因此 Ckarta Windows connection 必須由 connection lifetime 擁有 operation context，直到 completion 或 cancellation 終止後才能回收。

不能讓 stack-local WSAOVERLAPPED 在函式返回後仍被 outstanding operation 使用。

## 7. IOCP 對 Java→C completion 的啟示

Microsoft 官方將 IOCP 定位於高並行非同步 I/O；可搭配預先配置的 thread pool。

來源：
https://learn.microsoft.com/zh-tw/windows/win32/fileio/i-o-completion-ports

因此 Windows completion 的候選路徑為：
Java worker → JNI signal → PostQueuedCompletionStatus → C IOCP event loop。

這讓 network I/O completion 與 Java completion notification 可以進入相同的 Windows completion queue。

Linux 可研究：
Java worker → JNI signal → eventfd write → epoll_wait → C completion drain。

兩者 semantics 一致，但 platform primitive 不同。

## 8. completion notification 候選

Linux：
- eventfd：適合以 counter 表示「有 completion 可取」，可和 epoll 整合。
- pipe：一般性較高，適合作為 portability fallback。
- socketpair：可行但語意偏重，目前不是首選。

Windows：
- IOCP + PostQueuedCompletionStatus：最符合 Ckarta IOCP network backend。
- Event／WaitForSingleObject 類控制事件：較適合 control plane，不是高頻 completion 首選。

以上均為候選，不宣稱已完成 benchmark。

## 9. Ckarta 的統一通知抽象

上層不應暴露 Linux eventfd API 或 Windows IOCP API。

概念上應提供：
event_backend_wait
event_backend_submit_io
event_backend_post_completion
event_backend_cancel
event_backend_close

上層資料則保持：
event kind、owner token、operation token、bytes、status、context。

這個介面目前只是設計候選，尚未建立正式 public API。

## 10. 直接呼叫 Win32／Linux API 是否違反工作準則

結論：直接使用平台官方、文件化 API 本身不違反現有工作準則；把平台 API 散落在 portable core、跳過 ownership／error／lifetime 驗證，才會違反既有架構與安全原則。

允許集中在：
c/platform/linux/ → epoll、eventfd、socket、accept4 等。
c/platform/windows/ → IOCP、WSARecv、WSASend、AcceptEx、CreateEvent 等。

必須同時滿足：
1. 輸入長度、fd／HANDLE 狀態、OVERLAPPED 狀態先驗證。
2. 平台 error code 立即映射成 Ckarta 明確錯誤語意。
3. HANDLE、fd、OVERLAPPED、completion context 的 owner 與 lifetime 明確。
4. 不讓 portable core 到處散落 _WIN32／Linux 條件分支。
5. 不使用 raw syscall number（原始系統呼叫號碼）作一般介面。
6. Linux 優先使用 libc／官方 system-call wrapper，而不是直接 syscall(2)。
7. Windows 不依賴未文件化 NT Native API 或自行發出 system call。
8. 平台最佳化集中於 backend，並接受相同的型別、函式、生命週期、安全與測試規則。

## 11. 為何不採 raw syscall

Linux raw syscall 會讓 Ckarta 自己承擔 architecture-specific calling convention、syscall number、register ABI、error conversion 與 feature detection。

Windows 更不應把 undocumented NT system call 當正式應用程式介面。

因此 Ckarta 的「底層」應理解成：直接使用 documented OS API 或穩定的 system-call wrapper，而不是自行發出 raw kernel syscall。

## 12. portability 邊界

跨平台：HTTP semantics、request／response state machine、ownership、JNI ABI、Servlet semantics、module ABI、configuration model。

平台特化：socket creation、event backend、completion notification、timer primitive、file I/O、memory mapping、process/thread primitive、control mechanism。

因此 Windows 支援不意味所有 C 程式碼都必須被包成巨型 wrapper；應形成 small platform backend + portable core。

## 13. 學術依據

Matt Welsh、David Culler、Eric Brewer，SEDA: an architecture for well-conditioned, scalable Internet services。DOI：https://doi.org/10.1145/502059.502057。研究把網路服務切成 event-driven stages 與 explicit queues，支持 Ckarta 的分階段 completion 模型。

Vivek S. Pai、Peter Druschel、Willy Zwaenepoel，Flash: An Efficient and Portable Web Server，USENIX ATC 1999。來源：https://www.usenix.org/conference/1999-usenix-annual-technical-conference/flash-efficient-and-portable-web-server。研究比較 multi-process、multi-thread、single-process event-driven、AMPED，支持 concurrency architecture 必須和 workload／平台一起選擇。

Gaurav Banga、Jeffrey C. Mogul，Scalable Kernel Performance for Internet Servers Under Realistic Loads，USENIX ATC 1998。來源：https://www.usenix.org/conference/1998-usenix-annual-technical-conference/scalable-kernel-performance-internet-servers。研究顯示低階 kernel event mechanism 本身可能成為 server scalability bottleneck。

## 14. 決策與未做列表

Windows support：正式列為可行架構目標。
Windows primary event backend：IOCP + Overlapped Winsock I/O。
Windows select：fallback／測試 backend 候選。
Linux primary：epoll-based event backend。
Linux Java completion notification：eventfd 優先研究，pipe 作 portability fallback。
Windows Java completion notification：IOCP + PostQueuedCompletionStatus 優先研究。
CGI/FastCGI：仍為 optional module，不因 Windows 支援而進 core。
native raw syscalls：不採用。
documented OS APIs：允許，集中於 platform backend。

以上是架構研究與候選方案，不代表 Windows backend 或正式 completion notification 已完成。