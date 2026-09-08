# Ckarta 程式程序進入點設計

## 1. 決策

Ckarta 正式產品程序採 C main() 作為唯一外部進入點。

Java main-class 不作為正式 server（伺服器）程序入口。

## 2. 證據

### Nginx

固定版本：1.30.4，commit 017cf98dcce217946572a896f0992370475e189f。

檔案：src/core/nginx.c；已確認存在 int ngx_cdecl main(int argc, char *const *argv)。其啟動路徑會先建立初始化 cycle，處理設定，再依程序模式進入 ngx_master_process_cycle() 或 ngx_single_process_cycle()。

### Apache Tomcat

固定版本：11.0.25，commit cbe6e15ee81e2fc6232954292a80cca5d1e84009。

Bootstrap.java 已確認存在 public static void main(String[] args)，並由 Bootstrap init/load/start/stop 管理 Java container lifecycle（Java 容器生命週期）。

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
→ JVM_STARTING
→ JVM_READY
→ JAVA_CONTAINER_READY
→ NETWORK_READY
→ RUNNING

完整狀態轉移、rollback（回滾）及停止狀態見 docs/STARTUP_STATE_MACHINE.md。

## 5. 第一階段禁止

不得：

C main
→ JNI_CreateJavaVM
→ fork
→ child 繼承已啟動 JVM

多 JVM／多程序模型必須另行完成 fork safety（複製程序安全性）、JVM state、native thread state（原生執行緒狀態）與 shutdown 模型研究後才可採用。

## 6. C main 責任

C main 負責 process lifecycle（程序生命週期）、native configuration、native runtime、listener／socket、C worker、JVM bootstrap coordination（JVM 啟動協調）與 shutdown coordination（停止協調）。

C main 不直接執行 Servlet application code。

## 7. Java 責任

Java 負責 Java container initialization、web application deployment、Servlet lifecycle、class loading、Filter、Listener、Session、ServletContext、RequestDispatcher 與 AsyncContext。

## 8. OpenJDK 21 啟動基線

OpenJDK 21 Update 目前以 jdk-21.0.8-ga 作為 JNI／JVM 研究基線。這不改變 Jakarta Servlet 6.1 的規格平台要求。

JNI Invocation API 的使用必須與 C main、JVM bootstrap thread lifecycle（JVM 啟動執行緒生命週期）及 native thread attachment 規則一起設計。

## 9. 與 Nginx／Tomcat 的吸收

Nginx：吸收明確程序 lifecycle、master／worker 啟動與停止概念。

Tomcat：吸收明確 Java container init/load/start/stop lifecycle 與 classloader isolation（類別載入器隔離）概念。

兩者的私有 API、內部資料結構與實作細節不得直接變成 Ckarta ABI。
