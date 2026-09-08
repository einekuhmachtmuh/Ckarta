# Ckarta 程式程序進入點設計

## 1. 決策

Ckarta 正式產品程序採 C main() 作為唯一外部進入點。

Java main-class 不作為正式 server（伺服器）程序入口。

## 2. 證據

### Nginx

固定版本：1.30.4，commit 017cf98dcce217946572a896f0992370475e189f。

檔案：src/core/nginx.c；已確認存在 int ngx_cdecl main(int argc, char *const *argv)。其啟動路徑會先建立初始化 cycle，處理設定，再依程序模式進入 ngx_master_process_cycle() 或 ngx_single_process_cycle()。固定版本原始碼顯示 main() 在設定、OS／module 初始化與 cycle 建立完成後，才進入 single/master process cycle。

### Apache Tomcat

固定版本：11.0.25，commit cbe6e15ee81e2fc6232954292a80cca5d1e84009。

Bootstrap.java 已確認存在 public static void main(String[] args)。它以 Bootstrap instance 管理 init/load/start/stop 等 Java container lifecycle（Java 容器生命週期）；正式啟動仍由 Java 入口驅動。

## 3. Ckarta 為何採 C main

Ckarta 的外部生命週期是：

C main
→ native configuration（原生設定）
→ native runtime（原生執行環境）
→ JVM bootstrap（JVM 啟動）
→ Java Servlet container（Java Servlet 容器）
→ network runtime（網路執行環境）

因此 C 是程序 owner（擁有者），Java 是 JVM 內 Servlet semantics（Servlet 語意）的 owner。

Oracle JNI Invocation API 支援 native application（原生應用程式）建立 JVM：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html

## 4. 正式啟動狀態

START
→ ARGS_READY
→ NATIVE_CONFIG_READY
→ NATIVE_RUNTIME_READY
→ JVM_BOOTSTRAP_STARTING
→ JVM_READY
→ JAVA_CONTAINER_READY
→ NETWORK_READY
→ RUNNING

`C main()` 是正式程序入口；`JVM_BOOTSTRAP_STARTING` 代表受控的專用 native bootstrap thread（原生啟動執行緒）正在執行 `JNI_CreateJavaVM()`。JNI 規格指出呼叫 `JNI_CreateJavaVM()` 的執行緒會成為 JVM 的 main thread，且建議不要使用 primordial process thread（原始程序執行緒）直接載入 JVM，而應建立專用執行緒。

## 5. 第一階段禁止

不得：

C main
→ JNI_CreateJavaVM
→ fork
→ child 繼承已啟動 JVM

多 JVM／多程序模型必須另行完成 fork safety（複製程序安全性）、JVM state、native thread state（原生執行緒狀態）與 shutdown 模型研究後才可採用。

## 6. C main 責任

C main 負責 process lifecycle（程序生命週期）、native configuration、native runtime、listener／socket、C worker、JVM bootstrap coordination（JVM 啟動協調）與 shutdown coordination（停止協調）。

C main 本身不應把所有 JVM 工作直接塞進 primordial thread；應建立並管理 bootstrap thread，等待 JVM/container ready，再決定何時開放 network admission（網路准入）。

C main 不直接執行 Servlet application code。

## 7. Java 責任

Java 負責 Java container initialization、web application deployment、Servlet lifecycle、class loading、Filter、Listener、Session、ServletContext、RequestDispatcher 與 AsyncContext。

Tomcat 的 Bootstrap.java 可作 lifecycle decomposition（生命週期分解）的參考，但其反射、classloader 與 Catalina 私有內部並非 Ckarta ABI。

## 8. OpenJDK 21 啟動基線

OpenJDK 21 Update 目前以 jdk-21.0.8-ga 作為 JNI／JVM 研究基線。這不改變 Jakarta Servlet 6.1 的規格平台要求。

Invocation API 要點：

- `JNI_CreateJavaVM()` 建立 JVM，呼叫執行緒會附加為 main thread。
- `JNIEnv*` 僅對當前 native thread 有效。
- 其他 native thread 需 `AttachCurrentThread()` 或 `AttachCurrentThreadAsDaemon()`。
- attached native thread 結束前必須 `DetachCurrentThread()`。
- `DestroyJavaVM()` 會等待 non-daemon threads（非 daemon 執行緒）終止後才完成 JVM termination；因此 Ckarta 必須先停止／join（加入等待）自己的 native worker 與 JNI-attached thread，再進入 JVM destroy phase。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html

## 9. JVM bootstrap thread 與 JNI bridge

第一階段採：

C main
→ create bootstrap pthread（建立啟動 pthread）
→ bootstrap thread：JNI_CreateJavaVM
→ Java bootstrap / container init
→ 發出 JAVA_CONTAINER_READY
→ C main／control thread 收到 ready
→ 建立 JNI bridge thread／pool
→ 開放 network

C worker 不直接共享 `JNIEnv*`。第一階段暫不要求 C worker 永久 attach JVM；C worker 將完成解析的 request descriptor 放入 bounded JNI queue，再由 JNI bridge thread 取得當前 thread 的 `JNIEnv*` 執行粗粒度 JNI dispatch，最後以 completion record 回到原 owner worker。

這是 thread／ownership 的初步決策，不代表 bridge thread 一定比 C worker 直接 attach 更快；完整待測方案見 `docs/THREAD_MODEL.md`。

停止時：

stop admission
→ stop／drain JNI dispatch
→ finish/cancel Servlet work
→ stop C workers
→ detach remaining JNI-attached native threads
→ terminate Java container
→ DestroyJavaVM
→ join bootstrap／bridge／worker threads
→ native cleanup

真正的 thread join 順序、bridge thread 數量與 queue policy 必須以可執行測試及 benchmark 固定；本文件不假造尚未存在的 Ckarta API。

## 10. 與 Nginx／Tomcat／OpenJDK 的吸收

Nginx：吸收明確程序 lifecycle、master／worker 啟動與停止、設定完成後才進入服務 cycle 的結構；但 Ckarta 第一階段不採 JVM 已建立後 fork。

Tomcat：吸收明確 Java container init/load/start/stop lifecycle 與 classloader isolation（類別載入器隔離）概念，以及其 endpoint／executor 分層思維；不直接複製 Tomcat thread topology（執行緒拓撲）。

OpenJDK：採用 Invocation API 對 primordial thread、JNIEnv thread affinity（執行緒親和性）、native thread attachment 與 DestroyJavaVM 的明確生命週期要求。

三者的私有 API、內部資料結構與實作細節不得直接變成 Ckarta ABI。

## 11. 啟動設定載入

C main 在建立 JVM 前先載入並驗證主設定檔。第一版預設 conf/ckarta.conf，可由 -c 指定其他檔案；-t 只做設定驗證，-T 驗證後輸出正規化設定。設定快照生命週期覆蓋整個 JVM／native runtime 使用期間，完成 shutdown 後才釋放。

設定載入不是 Servlet container 設定的替代品；它只負責 native bootstrap 與 C runtime 所需設定。Java Servlet configuration 仍由 Java container 管理。
