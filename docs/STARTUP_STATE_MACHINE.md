# Ckarta 啟動狀態機

本文件把 C main()、JVM 啟動、Java Servlet container（Java Servlet 容器）、C network data plane（C 網路資料平面）與停止流程固定成顯式狀態。

## 1. 正常啟動

START
→ ARGS_READY
→ NATIVE_CONFIG_READY
→ NATIVE_RUNTIME_READY
→ JVM_STARTING
→ JVM_READY
→ JAVA_CONTAINER_READY
→ NETWORK_READY
→ RUNNING

C main 是正式程序入口。JVM 由 C 透過 JNI Invocation API 建立。

## 2. 狀態定義

ARGS_READY：命令列已解析。

NATIVE_CONFIG_READY：原生設定已驗證。

NATIVE_RUNTIME_READY：C memory／event／logging 等必要原生子系統已就緒。

JVM_STARTING：JVM 正在建立。

JVM_READY：JNI_CreateJavaVM() 已成功取得 JavaVM*。

JAVA_CONTAINER_READY：Java bootstrap 已建立可接受 request 的 Servlet container；此時仍未開放公開 listener。

NETWORK_READY：listener、TLS context、event backend 與 worker state 已就緒。

RUNNING：允許 C event worker 收受請求並把 Servlet request 分派到 Java executor。

## 3. 失敗回滾

任何狀態失敗都必須 fail closed（失敗關閉）。

JVM 失敗：native cleanup → EXIT。

Java container 失敗：Destroy JVM → native cleanup → EXIT。

Network 失敗：close listeners → stop Java container → Destroy JVM → native cleanup → EXIT。

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

先停止新 request admission（請求准入），再等待／取消既有 Servlet 工作與 AsyncContext。

## 5. Nginx 對照

固定版本 Nginx 1.30.4 的 `src/core/nginx.c` `main()` 負責初始設定與 cycle；`src/os/unix/ngx_process_cycle.c` 的 `ngx_master_process_cycle()` 管理 worker 啟動、訊號、停止與重配置。

來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/core/nginx.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/unix/ngx_process_cycle.c

## 6. Tomcat 對照

固定版本 Tomcat 11.0.25 的 `Bootstrap.main()` 管理 `init()`、`load()`、`start()`、`stopServer()`；CoyoteAdapter 與 Container lifecycle 在 Java 層執行。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Bootstrap.java

## 7. OpenJDK 21 對照

OpenJDK 21 Invocation API：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html

C main 啟動 JVM 後，JNIEnv 只屬於相應 native thread；不得跨執行緒共享 JNIEnv。

## 8. 實作前置門檻

第一個真正的 C entrypoint source file 建立前，必須確認：

- JVM bootstrap thread lifecycle
- native configuration ownership
- listener ownership
- Java container startup API
- worker startup API
- failure rollback order
- signal／shutdown model
- JVM destroy timing

本文件只描述狀態與責任，不假造尚未實作的 Ckarta API。
