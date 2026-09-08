# Ckarta 程式程序進入點設計

## 1. 決策

Ckarta 正式產品程序採 C main() 作為唯一外部進入點。

Java main-class 不作為正式 server（伺服器）程序入口。

## 2. 證據

### Nginx

固定版本：

1.30.4
commit 017cf98dcce217946572a896f0992370475e189f

檔案：

src/core/nginx.c

已確認存在：

int ngx_cdecl
main(int argc, char *const *argv)

其啟動路徑會先建立初始化 cycle，處理設定，再依程序模式進入：

ngx_master_process_cycle()
或
ngx_single_process_cycle()

Nginx 官方 development guide 也把 master process、worker process 與 single process 清楚分層。

來源：

https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/core/nginx.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/unix/ngx_process_cycle.c
https://nginx.org/en/docs/dev/development_guide.html

### Apache Tomcat

固定版本：

11.0.25
commit cbe6e15ee81e2fc6232954292a80cca5d1e84009

檔案：

java/org/apache/catalina/startup/Bootstrap.java

已確認存在：

public static void main(String[] args)

該 main() 先建立 Bootstrap，呼叫 init()，再依命令執行 load()、start() 或 stopServer()。

來源：

https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Bootstrap.java

## 3. 為什麼 Ckarta 選 C main

Ckarta 與 Tomcat 最大的差異不是 Servlet 語意，而是它的公開程序生命週期。

Ckarta 的外部生命週期包含：

命令列
→ C 設定
→ 原生資源
→ listener/socket（監聽器／通訊端）
→ TLS
→ C event loop（C 事件迴圈）
→ JVM
→ Servlet container（Servlet 容器）

因此 C 必須先取得程序控制權。

Oracle JNI Invocation API 明確允許 native application（原生應用程式）載入 Java VM；JNI_CreateJavaVM() 建立並初始化 Java VM。

來源：

https://docs.oracle.com/en/java/javase/17/docs/specs/jni/invocation.html

## 4. 啟動模型

第一階段單程序模型：

C main()
→ parse startup arguments（解析啟動參數）
→ load native configuration（載入原生設定）
→ initialize native subsystems（初始化原生子系統）
→ create Java VM
→ initialize Java container
→ bind／activate network listeners
→ start C event loop
→ run server

但實作時必須特別處理 Oracle 對 primordial process thread（初始程序執行緒）啟動 JVM 的警告。

Oracle 文件明確建議不要直接用 primordial thread 載入 JVM，而是建立專用新執行緒處理 JVM 建立。

因此：

C main()
→ JVM bootstrap thread（JVM 啟動執行緒）
→ JNI_CreateJavaVM()
→ Java bootstrap

並不等於：

C main()
→ JNI_CreateJavaVM()
→ C main thread 同時成為 Java application thread。

## 5. 不採 Java main 的理由

不是因為 Java main() 無法啟動伺服器；Tomcat 本身就是 Java main() 啟動。

而是 Ckarta 的設計目標不同：

Tomcat：
Java bootstrap
→ Java Connector
→ Java NIO
→ Servlet

Ckarta：
C bootstrap
→ C network data plane
→ JNI
→ Java Servlet container

若使用 Java main() 再反向載入整個 C data plane，C 將從架構上的 owner 變成 Java 啟動的附屬 native library（原生函式庫），這與 WORKING_RULES.md 的 C/Java 邊界相衝突。

## 6. 重要限制：fork 與 JVM

不允許：

C main()
→ JNI_CreateJavaVM()
→ fork()
→ child 繼承已建立 JVM

第一階段不採這個模型。

理由：

Ckarta 的多程序策略若存在，必須解決：

- JVM initialization semantics（JVM 初始化語意）
- fork safety
- classloader state（類別載入器狀態）
- native thread state（原生執行緒狀態）
- TLS state
- session state
- Java executor state

因此第一階段採單一 JVM 程序；平行化先使用多個 C worker thread（工作者執行緒）／event loop，再依 benchmark 決定是否需要多 JVM 程序。

## 7. C main 的責任邊界

C main 擁有：

- process lifecycle（程序生命週期）
- startup/shutdown coordination（啟動／停止協調）
- native configuration
- native memory subsystem
- listener setup
- C worker startup
- JVM bootstrap coordination

C main 不直接執行 Servlet.service()。

Java 負責：

- Java container initialization
- web application deployment（網頁應用程式部署）
- Servlet lifecycle
- application class loading
- Filter／Listener／Session
- Servlet AsyncContext

## 8. 關閉順序

推薦：

stop accepting new connections
→ signal C worker
→ stop new Servlet dispatch
→ wait for active Servlet work
→ complete／cancel AsyncContext
→ close remaining connections
→ stop Java container
→ destroy Java VM
→ destroy C resources
→ process exit

實際順序仍必須配合 Jakarta Servlet lifecycle 與 JVM termination semantics 實測確認。

## 9. 與 Nginx 的吸收

吸收：

- C process owner
- explicit startup lifecycle
- configuration before worker activity
- worker lifecycle
- signal-driven shutdown

不直接複製：

- ngx_cycle_t
- Nginx signal implementation
- Nginx module ABI

## 10. 與 Tomcat 的吸收

吸收：

- Java container bootstrap
- classloader isolation
- explicit container startup
- Servlet lifecycle

不直接複製：

- Bootstrap reflection implementation
- Catalina package hierarchy
- Tomcat Connector internals

## 11. 最終結論

正式 Ckarta server：

main()
= C

JVM：
由 C 啟動。

Servlet：
由 JVM 執行。

C event loop：
永不直接執行 Servlet application code。

此決策是目前的 architecture invariant（架構不變條件）。
