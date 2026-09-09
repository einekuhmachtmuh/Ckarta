# Ckarta

Ckarta 是以 Jakarta Servlet 6.1 為相容性目標的 Servlet container（伺服端小程式容器）與高效能 Web server（網頁伺服器）。

核心架構：

C 資料平面
+
JVM／Java Servlet 容器
+
嚴格定義的 JNI（Java 原生介面）邊界。

## Native connection / async ownership status

目前已完成可執行的 native connection registry／opaque handle／JNI async terminal arbitration slice：

`ServletRequest.startAsync()`
→ `CkartaAsyncCycleBinding`
→ package-private native bridge
→ native connection registry
→ terminal arbitration
→ Java AsyncContext semantic state

`ck_connection_t` 現已直接擁有 Linux/POSIX socket descriptor，並以 heap-backed reader owner 持有 65,536-byte HTTP connection input consumer；reader 在 `ck_connection_init()` 建立並於 connection terminal close 時釋放。

Linux `ck_event_loop` 已有獨立 executable baseline，並已完成 loopback TCP listener／accept integration smoke。

HTTP reader 現已透過 registry-safe reader pin 接入 loopback TCP integration：epoll readiness 經 handle/generation/identity validation 後取得短生命週期 pin，在不持有 registry mutex 的情況下執行 connection-owned non-blocking `recv()`、HTTP framing、body sink 與 pipelined leftover 處理，完成後 release pin。reader pin 存在時 registry 不允許 close/retire 回收 connection reader。

目前尚未完成：正式多 worker listener/accept ownership、read batching/fairness budget、完整 HTTP/1.1 request/response state machine、response ownership、async dispatch、real timeout source、完整 client-disconnect policy、shutdown drain、TCK、sanitizer/fuzz、Windows IOCP 與其他平台 event backend。
