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

第一個可執行 Linux backend 現已以 `c/event/ck_event_loop.[ch]` 落地，但仍維持平台中立的上層概念：register、modify、remove、wait、destroy，以及 opaque cookie 回傳。

Linux 實作：

- `epoll_create1(EPOLL_CLOEXEC)`。
- `epoll_ctl(EPOLL_CTL_ADD/MOD/DEL)`。
- `epoll_wait()`。
- level-triggered readiness。
- `uint64_t` opaque cookie；backend 不保存可被 registry retire 的 connection pointer。

正式 API 目前命名為 `ck_event_loop_*`，而不是把 epoll 專有名稱外洩至上層。未來可在不破壞上層 contract 的前提下，再把實作整理成更完整的 platform backend layering。

Windows 候選：IOCP + PostQueuedCompletionStatus。
Linux completion notification 候選：eventfd + epoll，現有 `ck_completion_notification` 已具備 eventfd 實作，但尚未與 `ck_event_loop` 建立正式 wakeup integration。

## 11. Java completion notification

目前多請求 completion routing 已經攜帶 request identity。下一步可把 completion notification 接至 platform event backend：Linux 以 eventfd 喚醒 epoll；Windows 以 PostQueuedCompletionStatus 注入 completion packet。

目前 Linux epoll event backend 已完成獨立 smoke slice；正式 completion-to-event-loop wakeup 仍未整合。

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
Windows IOCP backend：尚未實作。
正式 network listener／accepted connection integration：尚未實作。
