# Ckarta Win32／Linux 平台層研究

## 1. 決策摘要

Ckarta 應支援 Linux 與 Win32，但不得用「把 Unix API 一個一個在 Windows 上模擬」作為長期策略。

平台選擇只集中在少數 headers／source files。上層只依賴 Ckarta-defined platform-neutral contracts（平台中立契約）。

Linux 實作可直接使用 Linux／POSIX 原語；Windows 實作可直接使用 documented Win32／Winsock APIs。這不違反安全要求，前提是呼叫點被限制在 platform backend（平台後端）、所有外部長度與 handle lifecycle 均被驗證，而且不得把平台 handle 或 ABI 差異外洩到 Servlet／核心上層。

不建議直接以 Linux raw syscall numbers（原始系統呼叫編號）或 Windows undocumented／internal syscall（未文件化／內部系統呼叫）作為一般路徑。

## 2. 編譯期平台判斷

MSVC 明確定義 _WIN32；它在 x86、x64、ARM、ARM64EC 等 Windows 目標都會定義，不應拿 _WIN64 當作「是不是 Windows」判斷。
https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros

GCC 的 -Dname[=definition] 可以人工定義 preprocessor macro（前處理器巨集），但正式產品不應讓使用者任意用 -DCKARTA_WINDOWS 或 -DCKARTA_LINUX 偽造平台。應優先使用 compiler／target toolchain 的預定義平台 macro，再由單一 Ckarta platform header 轉換成 CK_PLATFORM_WINDOWS／CK_PLATFORM_LINUX。其他原始碼只檢查 CK_PLATFORM_*。
https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html

## 3. 為何不把條件編譯散落全專案

不應讓 connection、HTTP、timer、JVM、queue 等每個檔案都包含 _WIN32／__linux__ 分支。這會複製 platform branching、使 lifecycle rules 在不同 OS 漂移，也會使安全與錯誤處理產生不一致。

較佳：core/event/connection → platform-neutral contract；platform layer → linux／win32。只有 platform layer 知道 epoll、eventfd、IOCP、HANDLE、WSA API 等實體。

## 4. I/O 事件模型

Linux：epoll_wait() 等待 ready events；epoll 可使用 level-triggered 或 edge-triggered 模式。
https://man7.org/linux/man-pages/man7/epoll.7.html
https://man7.org/linux/man-pages/man2/epoll_wait.2.html

Windows：IOCP 是 completion-based model。CreateIoCompletionPort 可將多個 handles 關聯至同一 completion port；GetQueuedCompletionStatus 取得 completion 結果與 completion key；GetQueuedCompletionStatusEx 可一次取得多個 completion packet。
https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports
https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-createiocompletionport
https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatusex-func

Windows backend 不應強行模擬 epoll readiness 語意，而應將 IOCP completion 映射至相同 Ckarta event contract（事件契約）：accepted、read-ready-or-completed、write-ready-or-completed、timer、wakeup、error、closed。

## 5. Nginx 實際啟示

固定 Nginx 1.30.4 的 ngx_event.c 可見 core event layer 以抽象 event operations 組合不同平台策略；程式內同時存在 NGX_WIN32、NGX_USE_IOCP_EVENT、NGX_USE_LEVEL_EVENT、NGX_USE_CLEAR_EVENT 等平台／事件條件。
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/ngx_event.c

ngx_event_accept.c 依 IOCP 與非 IOCP 路徑設定 accepted socket 的 blocking／nonblocking policy。
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/ngx_event_accept.c

ngx_win32_init.c 直接使用 WSAStartup、WSAIoctl 取得 AcceptEx、TransmitFile、ConnectEx 等原生能力。
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/win32/ngx_win32_init.c

這支持 Ckarta 使用 native OS APIs，而不是為了跨平台硬套 POSIX facade。

## 6. Apache httpd 實際啟示

Apache 2.4 將 OS-specific concurrency strategy（作業系統特定並行策略）提升成 MPM；Windows 預設 mpm_winnt_module 使用 parent + child + worker threads，並使用 Windows 原生 networking features。
https://httpd.apache.org/docs/current/mod/mpm_winnt.html
https://httpd.apache.org/docs/current/mpm.html

因此 Ckarta 不應假設 Linux event loop 與 Windows event loop 是同一 implementation；應保證兩者提供相同 connection semantics（連線語意），但由不同 platform backend 實作。

## 7. Tomcat 實際啟示

Tomcat 11.0.25 的 Nio2Endpoint 使用 Java asynchronous channel abstraction 與 executor，而不把 Linux epoll 或 Windows IOCP 暴露給 Servlet application。
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/Nio2Endpoint.java

這進一步支持 OS mechanism 應留在 lower layer（下層）。

## 8. direct Win32／Linux API 是否違反安全規則

不違反，只要 native API 呼叫被限制在明確 platform backend、使用 documented API／公開 ABI、檢查所有 return value，並對 fd／HANDLE 的 ownership 與 close lifecycle 做明確管理。

禁止讓 fd／HANDLE 直接進 Java application；禁止把未驗證的外部設定值直接轉換為 platform API 參數；禁止用平台特定 handle 偽裝成另一平台的整數 ID。

## 9. raw system call 限制

Linux raw syscall 可以在理論上使用，但目前不採用。syscall number、架構、calling convention 與 kernel ABI 差異都會放大風險；已有公開 libc／Linux API 時，重做 syscall wrapper 沒有足夠理由。

Windows undocumented syscall 不採用。

若未來 benchmark 證明穩定公開 API 的 wrapper 真正是 hot-path bottleneck，才另立研究項目。

## 10. Ckarta platform event contract

第一個可執行 Linux backend 現已以 `c/event/ck_event_loop.[ch]` 落地，並進一步以 loopback TCP 驗證 listener／accept integration。上層維持平台中立概念：register、modify、remove、wait、destroy，以及 opaque cookie 回傳。

Linux 實作：

- `epoll_create1(EPOLL_CLOEXEC)`。
- `epoll_ctl(EPOLL_CTL_ADD/MOD/DEL)`。
- `epoll_wait()`。
- level-triggered readiness。
- `uint64_t` opaque cookie；backend 不保存可被 registry retire 的 connection pointer。
- loopback listener 使用 `accept4(..., SOCK_CLOEXEC | SOCK_NONBLOCK)` 建立 accepted descriptor。

正式 API 目前命名為 `ck_event_loop_*`，而不是把 epoll 專有名稱外洩至上層。正式多 worker platform backend layering 尚未完成。

Windows 候選：IOCP + PostQueuedCompletionStatus。
Linux completion notification 候選：eventfd + epoll，現有 `ck_completion_notification` 已具備 eventfd 實作，但尚未與 `ck_event_loop` 建立正式 wakeup integration。

## 11. Java completion notification

目前多請求 completion routing 已經攜帶 request identity。下一步可把 completion notification 接至 platform event backend：Linux 以 eventfd 喚醒 epoll；Windows 以 PostQueuedCompletionStatus 注入 completion packet。

目前 Linux epoll event backend 與 loopback TCP transport 已有獨立 executable smoke slice；正式 completion-to-event-loop wakeup 仍未整合。

## 12. filesystem 與 Unicode

Nginx Windows source 會將 UTF-8 轉為 UTF-16，再使用 CreateFileW、GetFileAttributesExW、MoveFileW 等 Unicode API。

因此 Ckarta internal path representation 應先固定 encoding／normalization policy（編碼／正規化政策），Windows backend 再做 UTF-16 轉換。不得讓每個上層模組自行呼叫 CreateFileA／CreateFileW。

## 13. 靜態檔案與 async I/O

Windows 後端可研究 TransmitFile、ReadFile／WriteFile OVERLAPPED 與 IOCP；Linux 後端可研究 sendfile 等。

Joubert 等人在 2001 年比較 Linux 與 Windows 2000 的高效能 Web server，指出 event notification、data movement、communication code path 均受 OS primitive 影響。
Philippe Joubert, Robert B. King, Rich Neves, Mark Russinovich, John M. Tracey, “High-Performance Memory-Based Web Servers: Kernel and User-Space Performance”, USENIX ATC 2001.
https://www.usenix.org/conference/2001-usenix-annual-technical-conference/high-performance-memory-based-web

JAWS 的 Windows NT 實驗也指出 I/O strategy、file size、load 會改變最佳方案，並實驗 asynchronous TransmitFile。
James C. Hu, Irfan Pyarali, Douglas C. Schmidt, “High Performance Web Servers on Windows NT: Design and Performance”, USENIX Windows NT Symposium 1997.
https://www.usenix.org/legacy/publications/library/proceedings/usenix-nt97/usage_abstracts/James_Hu.html

因此「Windows 必須模擬 Linux」沒有學術根據；應採原生機制並以相同 semantic contract 衡量。

## 14. 編譯配置策略

Linux + GCC／Clang：依 target toolchain 的預定義平台 macro。
Windows + MSVC：使用 _WIN32。
Windows + MinGW：依 target compiler 的 Windows predefined macro。

Ckarta-owned platform macros 只在單一 platform header 正規化。其他 source files 不直接依賴 compiler-specific OS macro。

build system 應負責 compiler、SDK、架構、feature probe 與第三方依賴；source code 不應依賴大量使用者手工 -D。

## 15. 安全與工作準則一致性

直接呼叫 native OS API 不等於降低安全性。真正危險的是 unchecked return value、handle lifetime 未定義、size_t 到平台整數截斷、pointer/handle 錯誤轉換、blocking API 進入 event loop、以及 Unicode path canonicalization 不一致。

因此 platform backend 必須接受與 JNI 一樣嚴格的 function signature、type conversion、lifetime、ownership、error path 與 security review。

## 16. 決策

Win32 支援：可行，保留為正式目標平台。
平台實作：OS-native。
上層介面：Ckarta-defined。
平台判斷：集中單一 platform header。
直接 Win32/Linux API：允許。
Linux raw syscall numbers：目前不採用。
Windows undocumented syscall：不採用。
IOCP 與 epoll：各自實作相同 Ckarta event contract。
Linux `ck_event_loop` baseline：已實作並由 GitHub Actions 驗證。
Linux loopback TCP listener／accepted connection integration：已實作並由 GitHub Actions 驗證。
Windows IOCP backend：尚未實作。
正式 multi-worker network event consumer：尚未實作。
正式 completion-to-event-loop wakeup：尚未實作。
HTTP framing／request-response network path：尚未實作。

## 17. Linux io_uring 受控 backend

除現有 epoll readiness backend 外，Linux 可採 io_uring completion backend。此 backend 不使用 liburing，僅在 platform/event backend 內透過 `<linux/io_uring.h>`、`syscall()` 與 `SYS_io_uring_setup/enter/register` 使用 Linux documented UAPI。

io_uring 與 epoll 並非同一 programming model：epoll 回報 readiness，io_uring 回報 operation completion。上層因此不得直接共享兩者的私有資料結構；應經 Ckarta-defined connection/completion contract 轉換。

kernel version 只能作初步 deployment gate；正式啟用必須同時檢查 `io_uring_setup` 是否被 kernel/security policy 允許，以及 `IORING_REGISTER_PROBE` 回報的 required opcode 與 feature flags。不可 hard-code syscall number。

目前產品策略仍是 epoll compatibility baseline + optional io_uring backend。preferred modern target 為 Linux 6.12+；5.7+ 可在實際 probe 通過時作初始 socket io_uring 相容層。若 setup 被 seccomp/container policy 拒絕或 required opcode 不存在，必須回退 epoll。

詳見 `docs/IO_URING_BACKEND_RESEARCH.md`。

## 18. POSIX 在 Win32 上的相容性邊界

「POSIX on Win32」不是單一實作，也不是單純的名稱對映；至少要區分 Microsoft UCRT、原生 Win32／Winsock、MinGW/winpthreads、Cygwin 與 WSL。它們提供的是不同層次、不同 ABI、不同語意強度的相容性。

Microsoft UCRT 明確說明它實作「large subset of the POSIX.1 C library」，但「is not fully conformant to any specific POSIX standard」。同一份 Microsoft compatibility 文件也列出 UCRT 尚未提供 `<threads.h>` threading support 及 `<stdatomic.h>` atomic support。故 UCRT 的 POSIX-like C API 不能視為完整 POSIX runtime，更不能由此推論 pthread、fork、signals、Unix process model 或 Unix socket ABI 存在。

https://learn.microsoft.com/en-us/cpp/c-runtime-library/compatibility?view=msvc-170

MSVC 的部分傳統 POSIX／Unix-style 名稱只是 CRT 相容名稱。例如 `open` 可透過 OLDNAMES.LIB 對映到 `_open`。這屬於 CRT naming/backward compatibility，不等於提供 Unix syscall semantics。

https://learn.microsoft.com/en-us/cpp/c-runtime-library/backward-compatibility?view=msvc-170

因此 Ckarta 不應以「UCRT 有 POSIX 函式」作為 platform backend 設計基礎，而應以 Windows 官方 API 的實際 handle、錯誤碼、非同步 I/O、Unicode 與 lifetime semantics 為依據。

## 19. pthread／POSIX threads 在 Win32 上的實際實作

MSVC／UCRT 並不提供 Linux/glibc 式 pthread API。若 Windows C/C++ 程式需要 pthread API，常見做法是額外使用 winpthreads 或 pthreads4w 之類 compatibility library。

mingw-w64 的 `winpthreads` 原始碼直接包含 `<windows.h>`，並以 Windows TLS、HANDLE、SEH、Win32 thread lifecycle 等機制實作 pthread API。其 `pthread.h` 也明示部分來源承自 Pthreads for Microsoft Windows，並列出 unsupported／`ENOTSUP` 項目與 Windows-specific extensions。這證明 winpthreads 是「以 Win32 primitive 實作 POSIX thread interface」，不是 Windows kernel 原生提供的 pthread ABI。

https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-libraries/winpthreads/include/pthread.h
https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-libraries/winpthreads/src/thread.c

pthreads4w 則明確將自己描述為 POSIX 1003.1c／相關 Unix 規格在 Microsoft Windows 上的 implementation，並要求 MSVC 或 MinGW 建置。

https://github.com/fwbuilder/pthreads4w/blob/master/README

所以 `pthread_*` 應被 Ckarta 視為可替換的 platform compatibility facility，而非 portable C11 primitive，也不是 Windows native performance baseline。正式 Windows backend 應先比較 native Win32 thread／synchronization 與 pthread compatibility library 的實際成本，再決定是否保留 pthread facade。

## 20. Windows 原生 thread 與 synchronization 實作邊界

Windows kernel 將 thread 視為基本 schedulable entity；每個 user-mode thread object 有對應的 kernel-mode thread object。Microsoft driver/kernel 文件也指出 dispatcher objects 包括 thread、event、semaphore、mutex、timer，等待操作會使 thread 進入 wait state。

https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/introduction-to-thread-objects
https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/introduction-to-kernel-dispatcher-objects

Win32 synchronisation 並不是單一路徑。Microsoft 目前文件把 SRW lock、critical section、mutex、semaphore、event 等分開；其中 SRW lock 通常在 user mode 快速處理、爭用時才可能進 kernel，critical section 也具有 user-mode fast path；cross-process mutex 則較重。故「pthread mutex = 一次固定 kernel syscall」或「Win32 lock 一定比 pthread 快」都不是可直接採用的模型。

https://learn.microsoft.com/en-us/windows/win32/sync/about-synchronization

建立 thread 時，Microsoft 文件建議：若 thread routine 會使用 CRT，應使用 `_beginthreadex`；直接使用 `CreateThread` 的 CRT process 必須遵守相應的 CRT lifecycle contract。`_beginthreadex` 回傳的 handle 可用同步 API 等待，並由 caller 關閉。

https://learn.microsoft.com/en-us/windows/win32/procthread/creating-threads
https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/beginthread-beginthreadex?view=msvc-170

這表示 Ckarta Windows worker thread 若使用 CRT、JNI 與 socket backend，不應把 `CreateThread`、`_beginthreadex`、pthread_create 三者視為只換函式名稱；thread entry、TLS、CRT state、join／handle cleanup 與 shutdown 都是 implementation contract 的一部分。

## 21. POSIX socket 與 Winsock 不是 ABI 等價物

POSIX/Linux socket 通常以 integer file descriptor 表示；Windows Winsock 使用 `SOCKET`，並以 `closesocket` 關閉。Windows API 由 `WSAGetLastError` 提供 Winsock-specific error，而不是把所有情況直接視作 POSIX `errno`。

https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-closesocket
https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-wsastartup

Windows 的 nonblocking socket 使用 `ioctlsocket(..., FIONBIO, ...)`；它與 Linux `O_NONBLOCK`／`fcntl` 並非同一 ABI。對 Ckarta 而言，更重要的是保存「nonblocking read/write、可觀測 would-block、close、error」語意，而不是假設兩邊可共用 `int fd` 的 implementation。

https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-ioctlsocket

Winsock 還提供 OVERLAPPED I/O。`AcceptEx` 可同時完成 accept、地址取得及首段資料接收，而且透過 completion ports 支援大量連線以少量 threads 處理。這是 Windows 原生 completion-oriented design，而不是 epoll readiness 的函式別名。

https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nf-mswsock-acceptex
https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports

尤其 `closesocket` 的官方說明指出，呼叫後 socket descriptor 可能立即被重用；同時對 pending overlapped operations 有取消與 `WSA_OPERATION_ABORTED` 行為。這使 Windows connection lifetime 必須把「HANDLE/SOCKET lifetime」與「pending I/O completion lifetime」分開管理。

https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-closesocket

## 22. Cygwin 與 WSL 不是 Ckarta 的 Win32 native ABI

Cygwin 透過 `cygwin1.dll` 提供大量 POSIX API functionality，並維持自己的 POSIX filesystem view、path translation、process／signal／socket 等語意。因此它很適合移植 Unix application，但其 native ABI 已經是 Cygwin compatibility environment，而不是單純的 Win32 API。

https://cygwin.com/
https://cygwin.com/cygwin-api/cygwin-api.html
https://cygwin.com/cygwin-ug-net/using.html

WSL1 則採 Windows kernel 中的 Linux syscall compatibility layer；Microsoft 文件描述其將 Linux syscall 導向 lxcore.sys，並在必要處翻譯為 Windows functionality。WSL2 則改採真正的 Linux kernel；它們都不能作為 Ckarta Win32 binary 的 native POSIX ABI。

https://learn.microsoft.com/en-us/windows/desktop/cmdline/wsl-architectural-overview
https://github.com/microsoft/WSL
https://blogs.windows.com/windowsdeveloper/2025/05/19/the-windows-subsystem-for-linux-is-now-open-source/

因此「在 Windows 上支援 POSIX」與「Ckarta Win32 backend 使用 POSIX ABI」是兩個不同問題。Ckarta 應選後者為否。

## 23. POSIX semantics 與 Windows-native semantics 的不可直接等價項

至少以下項目不可因為有一組同名或近似名函式，就假定語意相同：

1. `pthread_*` 與 Windows thread／TLS／handle lifecycle。
2. Unix integer file descriptor 與 Windows `HANDLE`／Winsock `SOCKET`。
3. `errno` 與 `WSAGetLastError`／`GetLastError`。
4. `fork()`／`exec*()` 與 CreateProcess；Windows 沒有原生、等價的 Unix `fork` process model。
5. POSIX signal model 與 Win32 console／process exception model。
6. `poll`／`epoll` readiness 與 IOCP completion。
7. Unix path／inode／mode semantics 與 Windows UTF-16／handle／ACL／reparse-point semantics。
8. POSIX cancellation points 與 Windows thread termination／cooperative cancellation。
9. `mmap`／`shm_open` 等 Unix process-shared memory facilities 與 Windows mapping handles。
10. `dlopen`／`dlsym` 與 LoadLibrary／GetProcAddress。

Ckarta 不應建立覆蓋全部項目的「POSIX facade」。這會把每個差異都升格成 compatibility burden，並使 native backend 無法利用 Windows completion、handle 與 Unicode 等本身的優勢。

## 24. Nginx、Windows kernel 與學術證據的綜合判斷

固定 Nginx 1.30.4 的 Windows source 已直接使用 WSAStartup、WSAIoctl、AcceptEx 等 Windows-native capabilities；其 event layer 本身就是依平台事件模型組合，而不是把 Linux epoll 強行移植成 Win32 API。這與 Ckarta platform isolation 的設計方向一致。

Windows kernel 官方文件把 thread 與 dispatcher/synchronization 明確當作原生 OS objects；Windows synchronization guidance 也區分快速 user-mode primitive 與 kernel-backed object。因此 Windows native thread backend 有其完整的原生語意，不必經 pthread compatibility layer 才能形成高效能並行模型。

學術上，SEDA 論證 explicit stages、queues、resource control 能改善高度併行網路服務的 overload 行為；Capriccio 則顯示 scalable thread-based server 是另一條可行路徑；Zeldovich 等人的 multiprocessor event-driven work 說明 event-driven architecture 也能有效利用多 CPU。這些工作共同支持「不要把 API 樣式直接等同於效能模型」：Ckarta 應以 ownership、queueing、I/O completion、CPU locality 與 workload benchmark 決定 topology，而不是預設 POSIX thread facade 必然最佳。

Nickolai Zeldovich, Alexander Yip, Frank Dabek, Robert T. Morris, David Mazières, Frans Kaashoek, “Multiprocessor Support for Event-Driven Programs”, USENIX ATC 2003.
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

Matt Welsh, David Culler, Eric Brewer, “SEDA: An Architecture for Well-Conditioned, Scalable Internet Services”, ACM SIGOPS Operating Systems Review 35(5), 2001, DOI 10.1145/502059.502057.
https://doi.org/10.1145/502059.502057

Rob von Behren, Jeremy Condit, Feng Zhou, George C. Necula, Eric A. Brewer, “Capriccio: Scalable Threads for Internet Services”, SOSP 2003, DOI 10.1145/945469.945471.
https://doi.org/10.1145/945469.945471

Philippe Joubert, Robert B. King, Rich Neves, Mark Russinovich, John M. Tracey, “High-Performance Memory-Based Web Servers: Kernel and User-Space Performance”, USENIX ATC 2001.
https://www.usenix.org/conference/2001-usenix-annual-technical-conference/high-performance-memory-based-web-servers-kernel

## 25. 最終平台決策

1. Ckarta 的 portable core 維持 ISO C11；POSIX、Win32、Winsock、JNI 均屬額外 platform/API contracts。
2. 不把 POSIX facade 視為 Win32 compatibility baseline。
3. Windows backend 應優先使用 documented Win32、Winsock、OVERLAPPED、IOCP、Unicode API 與 Windows synchronization primitives。
4. MinGW/winpthreads 只可視為 toolchain compatibility option；不得因 `-pthread` 能在某一 Windows toolchain 工作，就把 pthread ABI 變成 Ckarta 的 Windows architecture invariant。
5. Ckarta 上層只依賴自己的 platform-neutral contract；不得暴露 `int fd`、`SOCKET`、`HANDLE` 或 pthread-specific object 到 HTTP／Servlet layer。
6. `-pthread` 僅是目前 Linux/GCC CI build contract；Windows future build contract 應依實際 toolchain 分開定義，不得假設 MSVC 存在同義編譯選項。
7. 不修改 `WORKING_RULES.md` 建立大量 POSIX-specific rules。現有守則已要求 OS API 集中於 platform layer、portable core 與 platform contract 分離、documented API／UAPI、ownership／lifecycle／error 檢查及 benchmark 驗證；新增大批 pthread 規則會過度具體化。研究結論由本文件與 `docs/PTHREAD_COMPATIBILITY_RESEARCH.md` 保存。
8. Windows IOCP backend 仍未實作；本研究不構成 Windows CI 或 Windows runtime verified gate。
