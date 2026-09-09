# Ckarta Native Event Backend 基線

## 1. 目的

本文件是 Ckarta 原生事件後端的權威實作／契約文件。第一階段只定義 Linux/POSIX `epoll` baseline；Windows IOCP 與 BSD/macOS `kqueue` 不在本 slice 內實作。

事件後端只負責 I/O readiness／event notification demultiplexing，不取得 Ckarta connection ownership，也不直接執行 Servlet application code。

## 2. 固定架構位置

```text
C main
  ↓
C worker
  ↓
ck_event_loop
  ↓
ck_connection / HTTP state machine
  ↓
bounded semantic handoff
  ↓
Java Servlet plane
```

`ck_event_loop` 與 `ck_connection` 是兩個不同 ownership domain：

- event loop 擁有 epoll instance。
- connection 擁有 socket descriptor。
- connection registry 擁有 process-local opaque handle 的 lookup／lifetime authority。
- event notification 只攜帶 opaque cookie，不攜帶 `ck_connection_t *`。

## 3. Linux baseline

Linux backend 使用：

- `epoll_create1(EPOLL_CLOEXEC)` 建立 epoll instance。
- `epoll_ctl(EPOLL_CTL_ADD)` 註冊 descriptor。
- `epoll_ctl(EPOLL_CTL_MOD)` 更新 interest set 與 cookie。
- `epoll_ctl(EPOLL_CTL_DEL)` 移除 descriptor。
- `epoll_wait()` 取得 readiness notification。

目前刻意採 level-triggered semantics，不提前引入 `EPOLLET`；原因是第一個可執行 slice 必須先建立可驗證的 drain／re-arm contract，再以 benchmark 證明 edge-triggered 的收益是否值得複雜度。

目前 backend 將：

- `CK_EVENT_READ` → `EPOLLIN`
- `CK_EVENT_WRITE` → `EPOLLOUT`
- `CK_EVENT_RDHUP` → `EPOLLRDHUP`
- `CK_EVENT_ERROR` → `EPOLLERR | EPOLLHUP`

回傳時則將 `EPOLLIN`、`EPOLLOUT`、`EPOLLRDHUP`、`EPOLLERR/HUP` 映射回 Ckarta-defined event bits。

## 4. Opaque cookie 與 stale-event 防護

每個 registration 都有非零 `uint64_t` cookie。backend 只把 cookie 放入 Linux `epoll_event.data.u64`；不得把可被 registry retire 的 native pointer 放入 event data。

這個設計本身不是完整 stale-event proof。真正的生命週期安全仍要求 consumer 在收到 notification 後，以 registry 的 generation-protected handle 與 request／owner／lifetime identity 驗證 notification 是否仍指向當前 connection。

因此：

```text
epoll notification
→ opaque handle/cookie
→ registry validation
→ current connection
→ state machine
```

而不是：

```text
epoll notification
→ raw pointer
→ dereference
```

## 5. API contract

`c/event/ck_event_loop.h` 提供：

- `ck_event_loop_init()`：建立 event backend。
- `ck_event_loop_add()`：建立 fd + cookie + interest registration。
- `ck_event_loop_modify()`：以 `EPOLL_CTL_MOD` 更新 interest／cookie。
- `ck_event_loop_remove()`：移除 descriptor registration。
- `ck_event_loop_wait()`：blocking／non-blocking 取得 notification；`timeout_ms=-1` 表示無限等待。
- `ck_event_loop_destroy()`：釋放 epoll instance。

錯誤碼採正 errno for setup／control operation；`ck_event_loop_wait()` 的負值表示 `-errno`，零表示 timeout，正值表示實際 notification 數。

`ck_event_loop_wait()` 目前單次最多取 64 個 kernel events，再依 caller 提供的 `capacity` 截斷。這個固定值是 bootstrap 限制，不是最終 throughput tuning 結果。

## 6. Ownership、thread 與 blocking boundary

event backend 不擁有 socket descriptor。socket descriptor 必須先由 `ck_connection_t` 或更高層 listener owner 建立／接收，再由 owner 將 descriptor 註冊至 event backend。

目前 `ck_event_loop_t` 的正式 ownership contract 是 single-owner：同一個 event loop instance 在任一時間只能由一個 C event-loop owner thread 呼叫 `add`／`modify`／`remove`／`wait`／`destroy`。跨執行緒 registration 與 wakeup 尚未建立正式 contract，不得自行把目前 API 當成 thread-safe shared object。

event loop thread 可以呼叫 `ck_event_loop_wait()`，因為這就是其核心 blocking point；但在 notification dispatch 後不得執行未知 blocking application work。

未來 real network path 必須把：

- nonblocking socket configuration
- bounded read/write progression
- `EAGAIN/EWOULDBLOCK`
- partial read/write
- HUP/RDHUP/ERR
- close／retire ordering

明確放入 connection state machine。

## 7. Nginx 交叉核對

固定 Nginx 1.30.4 commit `017cf98dcce217946572a896f0992370475e189f` 的 `src/event/modules/ngx_epoll_module.c` 將 epoll backend 實作成 event module action，並在 Linux path 使用 `epoll_ctl()` 與 `epoll_wait()`；Nginx core 再把 event backend 與 connection/event handler 分層。

來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/modules/ngx_epoll_module.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/event/ngx_event.c

Ckarta 吸收的是「OS event backend 與 connection handler 分離」的架構原則，不複製 Nginx 私有 ABI 或資料結構。

## 8. Tomcat 交叉核對

固定 Tomcat 11.0.25 commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009` 的 `NioEndpoint` 使用 `Selector`、`SelectionKey`、socket wrapper 與 Poller；accepted socket 進入 `setSocketOptions()` 後會設定 non-blocking、建立／重用 channel／wrapper，再註冊至 Poller。Poller 的 ready key 再交給 processor。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/NioEndpoint.java

Ckarta 的差異是 Linux I/O demultiplexing 放在 C；Java 只在 semantic handoff 後進入 Servlet execution，不把 `SelectionKey` 或 Linux fd 暴露給 Servlet application。

## 9. 學術依據

SEDA：Matt Welsh、David Culler、Eric Brewer，
"SEDA: an architecture for well-conditioned, scalable Internet services"，ACM SIGOPS Operating Systems Review 35(5), 2001, 230–243。
DOI：https://doi.org/10.1145/502059.502057

SEDA 可支持將事件處理分成明確 stage 並控制 queue／overload，但不能據此宣稱 Ckarta 就是 SEDA。

Multiprocessor Support for Event-Driven Programs：Nickolai Zeldovich、Alexander Yip、Frank Dabek、Robert T. Morris、David Mazières，
"Multiprocessor Support for Event-Driven Programs"，USENIX ATC 2003, pp. 239–252。
來源：https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

這項工作顯示 event-driven system 可透過明確 concurrency partition 暴露平行性；Ckarta 因此保留 event loop 與 Java executor 的責任分界，但不把該研究的性能數字套用到 Ckarta。

## 10. 可執行驗證

`tests/event/ck_event_loop_test.c` 已驗證：

1. epoll instance 建立。
2. socketpair descriptor 註冊。
3. readiness notification 正確回傳。
4. cookie 在 event registration 後可被傳回。
5. `EPOLL_CTL_MOD` 可更新 cookie。
6. `EPOLL_CTL_DEL` 可移除 registration。
7. remove 後再次 remove 取得 `ENOENT`。
8. peer close 可產生 `RDHUP` 或 `HUP/ERR` 語意。

`tests/net/ck_tcp_event_integration_test.c` 進一步驗證：

1. loopback TCP listener 建立與 ephemeral port 取得。
2. client TCP connect 產生 listener readiness。
3. `accept4()` 取得 `SOCK_NONBLOCK | SOCK_CLOEXEC` accepted descriptor。
4. accepted descriptor attach 至 connection registry。
5. generation-protected opaque handle 作為 epoll cookie。
6. 真實 TCP request bytes 經 epoll readiness 後以 nonblocking `recv()` 取得。
7. data drain 後 `EAGAIN/EWOULDBLOCK` 被正確辨識。
8. client close 產生 `RDHUP` 或 `HUP/ERR` notification。
9. registration 先 remove，再進行 terminal、close、retire。
10. retire 後舊 generation handle 無法重新 lookup，新 connection 得到不同 generation handle。

GitHub Actions build-smoke 已在 Ubuntu 24.04、Temurin OpenJDK 21.0.12、GCC 13.3.0 執行 `make test`，其中 `ckarta-event-loop-test` 與 `ckarta-tcp-event-integration-test` 均實際建置及執行成功。

## 11. 尚未宣稱的能力

此 slice 不代表：

- 已完成一般 TCP listener／multi-worker accept architecture。
- 已完成 HTTP/1.1 request framing。
- 已完成完整 nonblocking read/write state machine。
- 已完成 TLS。
- 已完成 response output ownership。
- 已完成 real timeout timer source。
- 已完成完整 client-disconnect policy。
- 已完成 Servlet 6.1 runtime 或 TCK。
- 已證明 epoll 比其他 backend 更快。
- 已完成 Windows IOCP 或 macOS/BSD kqueue。

下一個網路實作閘門是把已驗證的 listener／accepted connection／epoll registration path 提升為正式 connection event consumer，再接入 HTTP/1.1 framing 與 bounded read/write state machine；stale notification 必須在 consumer 端以 generation/correlation identity 再驗證。
