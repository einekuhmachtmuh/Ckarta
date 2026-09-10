# Ckarta 2026-09-10 Win32 原始碼一致性稽核

本文件是 2026-09-10 對 `main` 的 source-level portability audit 紀錄。它不取代 `docs/WIN32_LINUX_PLATFORM_RESEARCH.md` 的長期平台策略，也不宣稱 Windows backend 已實作。

## 1. 稽核範圍

本次以 `WORKING_RULES.md`、`docs/WORK_STATE.md`、`docs/EVENT_BACKEND.md`、`docs/WIN32_LINUX_PLATFORM_RESEARCH.md`、`docs/HOT_PATH_REVIEW.md`、`docs/JNI_COST_MODEL.md` 為基礎，檢查 native C 的 platform／socket／event／connection／HTTP input/output／completion notification 邊界，並交叉核對固定 Nginx 1.30.4、Tomcat 11.0.25、Linux epoll／socket 語意、Microsoft UCRT/Winsock 與 libuv 的現行 source/documentation。

## 2. 已確認的不一致與修正

`c/connection/ck_connection.c` 原本直接呼叫 POSIX `close()`，即使該 socket 已由 `c/platform/ck_socket.c` 建立及 I/O，也仍把 platform API 洩漏到 connection layer。這與既有 platform-isolation policy 不一致。

同一函式在「connection 沒有 socket」的成功 close 路徑中還可能讀取尚未初始化的 `close_result`；這是 ISO C 層級的 undefined behavior，不是 Windows 專屬問題。

本次已將 connection close 改為經 `ck_socket_close()`，並初始化 `close_result`。此變更不新增 socket ABI，也不宣稱 Windows backend 已完成。

## 3. 尚未修正、但已確認的 Win32 portability blockers

### 3.1 socket handle type

`c/net/ck_tcp_listener.[ch]`、`c/connection/ck_connection.[ch]`、`c/connection/ck_connection_registry.[ch]`、`c/http/ck_http_connection_reader.[ch]`、`c/output/ck_http_output_writer.[ch]` 與相關測試目前仍以 Linux `int socket_fd` 作為 public/native handle contract。

這在 Linux 有效，但不能作為 Win32 socket ABI。Winsock 的 socket type 是 `SOCKET`，而不是 POSIX `int fd`；libuv 亦將 Windows `uv_os_sock_t` 定義為 `SOCKET`，並把它與 `uv_file`、`uv_os_fd_t` 分開，顯示 socket handle 與一般 file/OS handle 不應混為同一型別。

因此後續應採一次完整 migration，而非逐檔改型別。最小一致邊界應包括：socket type、invalid sentinel、create/accept ownership、recv/send result、close semantics、event registration、connection registry、HTTP reader/output writer 與測試。

### 3.2 socket error contract

目前 `ck_http_connection_reader.c` 與 `ck_http_output_writer.c` 在呼叫 platform socket I/O 後直接檢查 `errno`。未來 Win32 backend 若使用 Winsock，原生錯誤來源是 `WSAGetLastError()`；不能要求上層直接理解 Winsock error namespace。

後續 migration 應由 platform layer 將 `WOULD_BLOCK`、`INTR`/retry、peer-closed、fatal I/O error 等語意正規化；native diagnostic code 可保留在 platform layer。是否以 errno-compatible translation 或小型 Ckarta I/O result type 實作，需以 ABI 複雜度與 hot-path成本共同決定。

### 3.3 event-loop descriptor contract

`c/event/ck_event_loop.[ch]` 現在公開 `int` descriptor 與 Linux `epoll_fd`，本身是 Linux backend implementation，而不是可直接套用 Win32 IOCP 的 portability abstraction。

這一層不能只把 `int` 改名為 `ck_socket_t`：目前 completion notification 使用 Linux `eventfd`，因此 event loop 同時承載 socket 與非-socket wakeup descriptor。Windows IOCP 以 completion packet 為核心，不存在對等的「把 SOCKET/FD 註冊後等待 readiness」語意。

因此 event backend 應在 socket handle migration 之後另立 completion/wakeup abstraction，避免以單一 integer handle 假裝 Linux epoll、eventfd 與 Windows IOCP 的共同 native object。

### 3.4 completion notification

`c/event/ck_completion_notification.[ch]` 現在直接使用 Linux `eventfd`、`read()`、`write()` 與 `close()`，公開 `int fd`。這是 Linux-specific backend implementation，並非可直接移植到 Win32 的 notification contract。

現有 smoke path 可以保留 Linux eventfd；但若進入 Win32 backend，應改成平台中立的 completion notification interface。Windows 候選應研究 `PostQueuedCompletionStatus()` 注入 IOCP completion queue，而不是模擬 `eventfd` readable state。

## 4. 測試邊界

`tests/net/ck_tcp_event_integration_test.c` 與 `tests/connection/ck_connection_test.c` 直接使用 `socketpair()`、`socket()`、`connect()`、`send()`、`recv()`、`close()` 與 Linux/POSIX headers。這些測試目前可被視為 Linux backend tests，不能被解讀成跨平台 test coverage。

正式引入 Win32 backend 時，應新增 platform-specific integration test，驗證相同 Ckarta semantic contract，而非在測試程式內建立一個 POSIX-on-Windows compatibility facade。

## 5. build system

目前 Makefile 的 JNI include path 使用 `$(JAVA_HOME)/include/linux`，native CI 以 Ubuntu/GCC + OpenJDK 21 為 build contract；這與目前 Windows backend「尚未實作」的狀態一致，因此現在不應偽造 Windows CI 綠燈。

待 Win32 backend 真正進入 implementation phase，再新增 Windows toolchain/SDK/MinGW 或 MSVC build target，以及對應的 JNI include layout。不要在 Linux CI 尚未驗證之前以條件編譯製造未測的 Windows claim。

## 6. Nginx / Tomcat / Linux / open-source cross-check

Nginx 1.30.4 的 Win32 source 使用 `SOCKET`、Winsock initialization 與 AcceptEx/TransmitFile/ConnectEx 等 Windows-specific APIs；其 native I/O boundary 並未以 POSIX `int fd` 假裝 Windows socket。

Tomcat 11.0.25 的 Nio2 endpoint 透過 `AsynchronousServerSocketChannel`、`AsynchronousSocketChannel`、`AsynchronousChannelGroup` 與 completion handlers 管理 asynchronous I/O，Servlet layer 不直接依賴 OS native handle。

Linux epoll 本質上是 readiness notification；Windows IOCP 是 completion notification。因此 Ckarta 的共同抽象應落在「connection state / operation completion / lifecycle / ownership」等 semantic contract，而不是 native descriptor representation。

libuv 的 Windows header 將 `uv_os_sock_t`、`uv_os_fd_t`、`uv_file` 分開，提供了一個成熟開源專案對 socket／generic OS handle／file 三種 native resource 分離的直接 precedent。

## 7. 決策

目前立即修正：

- connection layer 的直接 `close()` platform leak。
- connection close 未初始化值 UB。
- 將本次 Win32 audit 結論持久化。
- 將「performance-guided simplicity」寫入 canonical engineering policy。

下一個 native portability slice 不應再做半套修改；應一次完成 socket handle contract migration，並以 Linux CI 為第一個驗證環境。

完成 socket migration 後，下一個獨立 slice 才處理 completion notification/event backend portability，最後才進入 Windows IOCP backend implementation。

## 8. 主要來源

- Microsoft, UCRT Compatibility：https://learn.microsoft.com/en-us/cpp/c-runtime-library/compatibility?view=msvc-170
- Microsoft Winsock `closesocket` documentation：https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-closesocket
- Microsoft I/O completion ports：https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports
- libuv `uv-win.h`：https://github.com/rwinlib/libuv/blob/master/include/uv-win.h
- Nginx 1.30.4 `ngx_win32_init.c`：https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/win32/ngx_win32_init.c
- Tomcat 11.0.25 `Nio2Endpoint.java`：https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/Nio2Endpoint.java
- Linux `epoll(7)` documentation：https://man7.org/linux/man-pages/man7/epoll.7.html
