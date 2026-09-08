# Ckarta 啟動配置載入與驗證研究

## 1. 決策摘要

Ckarta 正式程序由 C main() 啟動。第一版啟動配置採：

C main
→ 命令列解析
→ 載入單一主設定檔
→ 語法驗證
→ 指令語意驗證
→ 建立 C-owned configuration snapshot（C 擁有的設定快照）
→ JVM bootstrap
→ Java Servlet container init
→ network runtime init
→ RUNNING

目前不實作設定熱重載；但設定資料結構先按 immutable snapshot（不可變快照）設計，使未來可建立 new snapshot → validate → prepare → atomically switch ownership 的重載模型。

第一版設定語法採接近 Nginx／Apache 的 directive（指令）形式：

class_path "build/classes";

只允許單行指令、參數、分號與 # 註解；目前不支援區塊。這不是 Nginx 或 Apache 的相容設定語法，而是 Ckarta 自己的最小設定語言。

## 2. 為何不直接採 Tomcat XML

Tomcat 11.0.25 的 Catalina 使用 Digester 解析 server.xml，將 XML 元素映射成 StandardServer、StandardService、Connector、Executor、SSLHostConfig 等 Java 物件，並由 Java lifecycle 管理 init/start/stop。

固定原始碼：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Catalina.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Bootstrap.java

這對純 Java container 很合理，但 Ckarta 的核心設定包含 C event loop、socket、TLS、memory pool、process/resource limits 等原生資源。若先建立 Java configuration object graph（設定物件圖），再由 C 反向查詢，會把本應由 C 擁有的資源生命週期與 Java 物件生命週期耦合。

因此 Servlet configuration（Servlet 設定）可以由 Java 自己擁有；native server configuration（原生伺服器設定）先由 C 解析與擁有。

## 3. Apache HTTP Server

Apache HTTP Server 2.4.68 的 server/main.c 先處理命令列、建立 APR（Apache Portable Runtime）程序環境與設定所需的 process pools（程序記憶體池），再進入設定處理。

原始碼：
https://github.com/apache/httpd/blob/2.4.68/server/main.c

server/config.c 將設定系統與 module configuration vectors（模組設定向量）分離，並提供 pre_config、check_config、post_config、open_logs、child_init 等 hooks（鉤子）。

原始碼：
https://github.com/apache/httpd/blob/2.4.68/server/config.c

官方 httpd 文件也提供 -t syntax test、-T、-M、-S 等診斷／驗證模式。

來源：
https://httpd.apache.org/docs/current/programs/httpd.html

Apache 的優點是：

- 模組可以擁有自己的設定資料。
- 設定階段與 request 階段分離。
- check_config 讓模組在啟動前檢查設定。
- per-server／per-directory configuration（每伺服器／每目錄設定）可以合併。
- MPM 本身也是模組，因此基本並行模型可配置。

缺點是：

- 設定系統非常複雜。
- 模組設定向量與多層 merge rules（合併規則）會增加實作複雜度。
- 這種通用性對 Ckarta 第一階段而言過度。

因此 Ckarta 只吸收「directive → handler → validate → snapshot」概念，不直接複製 Apache configuration vector。

## 4. Nginx

固定 Nginx 1.30.4 的 src/core/nginx.c 先解析命令列，再建立 init_cycle，呼叫 ngx_process_options、ngx_preinit_modules、ngx_init_cycle。

原始碼：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/core/nginx.c

ngx_conf_parse 實作 directive、block、include 等設定語法；core module 與各 HTTP module 可以透過 module command table 接收自己的設定。

原始碼：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/core/ngx_conf_file.c

更重要的是，ngx_init_cycle 會：

- 建立新的 cycle pool
- 建立 module configuration
- 解析設定
- 執行 module init_conf
- 測試／建立 pid 與必要檔案
- 建立新的 cycle

reconfigure 時，ngx_master_process_cycle 重新建立 cycle；若失敗則保留舊 cycle，不把錯誤設定直接替換正在服務的設定。

原始碼：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/unix/ngx_process_cycle.c

這是 Ckarta 未來 configuration reload 最重要的設計啟示。

## 5. Tomcat

固定 Tomcat 11.0.25 使用：

Bootstrap
→ Catalina
→ server.xml
→ Digester
→ StandardServer / Service / Connector / Executor
→ init()
→ start()

Catalina.load() 會建立並設定 Server，再呼叫 Server.init()。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Catalina.java

StandardService.initInternal() 再初始化 Engine、Executor、MapperListener 與 Connector。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/StandardService.java

Tomcat 的優點是：

- configuration 與 component lifecycle（元件生命週期）緊密結合。
- server.xml 可以描述 Connector、Executor、SSL 與 web container hierarchy（Web 容器階層）。
- catalina.base 讓多 instance configuration（多實例設定）有清楚目錄邊界。

缺點是：

- XML 與 Digester object construction 對 C native runtime 不自然。
- 解析設定與建立 Java component object graph 已直接綁定。
- 設定重載在一般使用模型中通常以 restart 為主要方式。

## 6. Ckarta 採用的混合模型

### 外部命令列

第一版只處理啟動控制：

- -c：指定主設定檔。
- -t：只驗證設定並退出。
- -T：驗證並輸出正規化設定。
- -h：顯示說明。

這些命名與 Nginx／Apache 相近，但不是聲稱與兩者相容。

### 主設定檔

第一版 default path：

conf/ckarta.conf

目前最小 directive：

class_path "build/classes";

class_path 是 C-side bootstrap 設定，因為 JVM 建立前 C 必須已知道 Java class path。

### 設定處理階段

1. Parse：只處理 lexical syntax（詞法語法）。
2. Resolve：建立實際字串值。
3. Validate：檢查範圍、重複、互相依賴與安全限制。
4. Prepare：確認必要資源可取得。
5. Commit：產生 configuration snapshot。
6. Runtime init：只使用已驗證的 snapshot。

不能在 Parse 階段直接建立 listener、JVM、worker 或其他不可逆 runtime state。

## 7. 為何採設定快照

以設定狀態 C 與執行期狀態 R 表示：

C0 → validate → C1

只有在 Valid(C1) = true 後，才允許：

(C1, R0) → (C1, R1)

而不是：

C_partial → R_partial

這能將「設定錯誤」與「執行期半初始化狀態」分離。

未來 reload 可以：

C0 → C1

並只有當 C1 完整準備後才替換現行設定引用。

## 8. 配置錯誤的學術證據

Xu 等人在 OSDI 2016 的研究發現，多數成熟系統的重要設定參數沒有在 initialization phase（初始化階段）被特別檢查；其中 14.0%–93.2% 的重要設定缺乏專門初始化檢查。研究因此提出 PCHECK，透過模擬設定值後續使用來提早發現錯誤。

論文：
Tianyin Xu, Xinxin Jin, Peng Huang, Yuanyuan Zhou, Shan Lu, Long Jin, Shankar Pasupathy,
“Early Detection of Configuration Errors to Reduce Failure Damage”,
OSDI 2016, pp. 619–634.

可閱讀來源：
https://www.usenix.org/conference/osdi16/technical-sessions/presentation/xu

Sun 等人在 OSDI 2020 進一步研究 configuration changes（設定變更）的 context testing（情境測試），指出僅做靜態語法檢查不足以保證設定變更安全。

論文：
Xudong Sun, Runxiang Cheng, Jianyan Chen, Elaine Ang, Owolabi Legunsen, Tianyin Xu,
“Testing Configuration Changes in Context to Prevent Production Failures”,
OSDI 2020, pp. 735–751.

來源：
https://www.usenix.org/conference/osdi20/presentation/sun

因此 Ckarta 應要求：

syntax validation
+
range validation
+
cross-field validation
+
resource validation
+
security validation

全部在真正啟動不可逆 runtime 前完成。

## 9. 啟動與安全邊界

設定中的任何：

path
address
port
buffer size
worker count
timeout
certificate path
module path
Java class path

都不能直接使用外部字串而跳過範圍與生命週期檢查。

特別是：

- 不得在設定解析階段建立 socket。
- 不得因設定檔指定任意路徑而載入未授權的 native module。
- 不得讓設定值直接變成 shell command。
- 不得讓設定檔修改已存在 request／connection ownership。
- 不得讓設定物件持有已由 runtime 接管的暫時 buffer。

## 10. 啟動設定與模組邊界

核心只提供：

configuration parser
→ directive registry
→ validated configuration snapshot

未來模組才註冊：

directive
→ module-owned configuration

這與 Nginx 的 module command table 以及 Apache 的 module configuration／hook 架構概念一致。

CGI/FastCGI 因而可以在未來成為：

optional module
→ register its own directives
→ create module-owned configuration
→ handler only when selected by route

而不必使核心配置模型知道 CGI protocol。

## 11. 命令列與設定檔優先級

第一版規則：

- -c 只決定設定檔位置。
- 不讓環境變數默默覆蓋設定檔。
- 不讓命令列任意覆蓋安全相關設定。
- 未來若加入明確 command-line override，必須逐設定定義 precedence（優先順序）與驗證規則。

理由：Apache 的 -C／-c／-D 很強大，Nginx 的 -g 也可以直接提供 global directive；但 Ckarta 第一階段若允許大量命令列文字覆蓋，會使設定來源與安全稽核變得複雜。

## 12. 路徑基準

目前 class_path 相對路徑依程序 current working directory（目前工作目錄）解析。

不把這個行為假裝成 Nginx prefix 或 Tomcat catalina.base。

未來正式 runtime configuration root（執行期設定根目錄）確立後，應改成：

installation root / runtime root
+
relative resource path

而不是依啟動者目前工作目錄決定。

## 13. 理想啟動流程

最終目標：

C main
→ parse args
→ establish logging
→ load config
→ validate config
→ load optional modules
→ module config validation
→ freeze configuration
→ prepare native resources
→ create JVM
→ initialize Java Servlet container
→ prepare network workers
→ commit network admission
→ RUNNING

任何一步失敗：

shutdown prepared resources in reverse dependency order
→ destroy JVM if created
→ native cleanup
→ exit non-zero

## 14. 未來 reload

學習 Nginx：

old snapshot
+
new config
→ parse/validate/prepare new snapshot
→ create new runtime resources
→ switch admission
→ drain old runtime
→ release old snapshot

不能模仿 Tomcat 的「修改檔案後重新啟動」作為唯一模型。

第一階段不實作 reload；文件先保留這個不可變 snapshot boundary。

## 15. 研究限制

Apache、Nginx、Tomcat 的 startup models（啟動模型）是不同產品與不同目標下演化的實作，沒有一個可以直接稱為「最正確」。

因此本研究只把：

- Apache 的 module/config hook separation
- Nginx 的 cycle／validation／reload
- Tomcat 的 component lifecycle／configuration isolation

作為設計素材。

不宣稱 Ckarta 已經達到任何一者的成熟程度。

## 16. 主要來源

Apache HTTP Server：
https://github.com/apache/httpd/blob/2.4.68/server/main.c
https://github.com/apache/httpd/blob/2.4.68/server/config.c
https://httpd.apache.org/docs/current/programs/httpd.html
https://httpd.apache.org/docs/current/programs/apachectl.html

Nginx：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/core/nginx.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/core/ngx_conf_file.c
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/os/unix/ngx_process_cycle.c
https://nginx.org/en/docs/switches.html

Tomcat：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Bootstrap.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Catalina.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/CatalinaProperties.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/core/StandardService.java

學術研究：
https://www.usenix.org/conference/osdi16/technical-sessions/presentation/xu
https://www.usenix.org/conference/osdi20/presentation/sun
