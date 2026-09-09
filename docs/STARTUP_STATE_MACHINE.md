# Ckarta 啟動狀態機

本文件把 C main()、JVM 啟動、Java Servlet container（Java Servlet 容器）、C network data plane（C 網路資料平面）與停止流程固定成顯式狀態。

## 0. Target state vs current executable slice

本文件固定的是目標啟動／停止狀態機；目前 executable smoke 只實作 C main → config → JVM bootstrap → Java runtime → JNI request/completion → shutdown，尚未實作 NATIVE_RUNTIME_READY、NETWORK_READY 或 RUNNING 的完整 network data plane。不能把 smoke path 缺少的狀態寫成已完成。

## 1. 正常啟動

START
→ ARGS_READY
→ CONFIG_FILE_LOADED
→ NATIVE_CONFIG_VALIDATED
→ NATIVE_CONFIG_READY
→ NATIVE_RUNTIME_READY
→ JVM_BOOTSTRAP_STARTING
→ JVM_READY
→ JAVA_CONTAINER_READY
→ NETWORK_READY
→ RUNNING

C main 是正式程序入口。JVM 由 C 透過 JNI Invocation API 建立，但第一階段由專用 bootstrap thread（啟動執行緒）執行 `JNI_CreateJavaVM()`。

## 2. 狀態定義

ARGS_READY：命令列已解析。

NATIVE_CONFIG_READY：原生設定已驗證。

NATIVE_RUNTIME_READY：C memory／event／logging 等必要原生子系統已就緒。

JVM_BOOTSTRAP_STARTING：C main 已建立受控 bootstrap thread，該執行緒正在建立 JVM。

JVM_READY：`JNI_CreateJavaVM()` 已成功取得 `JavaVM*` 與該 bootstrap thread 的 `JNIEnv*`。

JAVA_CONTAINER_READY：Java bootstrap 已建立可接受 request 的 Servlet container；此時仍未開放公開 listener。

NETWORK_READY：listener、TLS context、event backend 與 worker state 已就緒。

RUNNING：允許 C event worker 收受請求並把 Servlet request 分派到 Java executor。

## 3. 失敗回滾

任何狀態失敗都必須 fail closed（失敗關閉）。

JVM 建立失敗：bootstrap thread 回報錯誤 → native cleanup → EXIT。

Java container 失敗：停止 Java bootstrap → Destroy JVM → native cleanup → EXIT。

Network 失敗：close listeners → stop Java container → 等待／取消 JNI-attached work → Destroy JVM → native cleanup → EXIT。

rollback（回滾）操作必須可重入並具有 idempotent（冪等）語意。

## 4. 停止

RUNNING
→ DRAINING
→ JAVA_DRAINING
→ CONNECTION_DRAINING
→ JAVA_STOPPING
→ JVM_DESTROYING
→ NATIVE_CLEANUP
→ EXITED

先停止新 request admission（請求准入），再等待／取消既有 Servlet 工作與 AsyncContext；所有 JNI-attached native thread 必須在退出前完成 detach。

`DestroyJavaVM()` 不能被當成任意背景清理動作；它會等待 non-daemon threads（非 daemon 執行緒）終止，因此 shutdown controller（停止控制器）必須先建立明確的 thread ownership 與 join 順序。

## 5. Nginx 對照

固定版本 Nginx 1.30.4 的 `src/core/nginx.c` `main()` 負責初始設定與 cycle；`src/os/unix/ngx_process_cycle.c` 的 `ngx_master_process_cycle()` 管理 worker 啟動、訊號、停止與重配置。main() 先完成 option、OS、module 與 cycle 初始化，再進入 single/master process cycle。

來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/core/nginx.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/unix/ngx_process_cycle.c

## 6. Tomcat 對照

固定版本 Tomcat 11.0.25 的 `Bootstrap.main()` 先初始化 `Bootstrap`，再依 command 執行 `load()`、`start()`、`stopServer()` 等 lifecycle；Java container 與 Coyote pipeline 在 Java 層執行。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Bootstrap.java

## 7. OpenJDK 21 對照

OpenJDK 21 Invocation API：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html

規格明確指出：`JNI_CreateJavaVM()` 的呼叫執行緒會成為 JVM main thread；並建議不要用 primordial process thread（原始程序執行緒）直接載入 JVM，而應建立專用執行緒。`JNIEnv*` 只對當前 thread 有效；attached native thread 退出前必須 detach。

## 8. 實作前置門檻

第一個真正的 C entrypoint source file 建立前，必須確認：

- C main 如何建立與等待 bootstrap thread。
- JVM bootstrap thread 如何傳遞 `JavaVM*`、ready/error 狀態與例外結果。
- native configuration ownership
- listener ownership
- Java container startup API
- worker startup API
- failure rollback order
- signal／shutdown model
- JVM destroy timing
- 所有 JNI-attached native thread 的 attach／detach／join ownership

本文件只描述狀態與責任，不假造尚未實作的 Ckarta API。

## 9. 配置載入閘門

CONFIG_FILE_LOADED 表示已成功讀取主設定檔；NATIVE_CONFIG_VALIDATED 表示語法、指令參數與目前實作可證明的安全條件均通過。只有到 NATIVE_CONFIG_READY 才能把設定快照交給 JVM／native runtime。任何設定錯誤都必須在建立不可逆 runtime state 前 fail closed。
