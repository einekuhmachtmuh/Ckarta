# Ckarta Native Module 架構與效能研究

## 1. 研究範圍

本文件研究 Ckarta native module（原生模組）的註冊、載入、設定、初始化、request hook、停止與版本相容性；本輪不實作 module loader。

CGI/FastCGI 明確維持 future optional module（未來可選模組），不進 core。

## 2. Apache httpd

Apache HTTP Server 2.4 以 module structure、module configuration vectors、hooks 與 DSO（動態共享物件）支援模組化。

server/config.c 具有 pre_config、check_config、post_config、open_logs、child_init、handler、quick_handler 等 hook。

mod_so 提供 LoadModule，於 server startup/restart 時載入 DSO；模組本身以 module symbol 向核心註冊。

官方資料：
https://httpd.apache.org/docs/current/dso.html
https://httpd.apache.org/docs/2.4/mod/mod_so.html
https://github.com/apache/httpd/blob/2.4.68/server/config.c

優點：

- 模組可擁有自己的設定。
- 設定與 request hook 分離。
- DSO 可獨立建置。
- 可以在設定檔中明確指定模組。

缺點：

- ABI、module vector、hook ordering 與 config merge 複雜。
- 同一 request 可能穿越多個 hook。
- 動態模組安全邊界非常高，載入即執行任意 native code。

## 3. Nginx

Nginx 1.30.4 的 ngx_module.c 提供：

ngx_preinit_modules()
ngx_cycle_modules()
ngx_init_modules()
ngx_count_modules()
ngx_add_module()

ngx_add_module() 實際檢查：

- dynamic module count
- module version
- NGX_MODULE_SIGNATURE binary compatibility
- duplicate module name
- module index
- module ordering

之後加入 cycle->modules。

來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/core/ngx_module.c

ngx_core_module 以 load_module directive 載入動態模組。

來源：
https://github.com/nginx/nginx/blob/017cf98dcce217946572a896f0992370475e189f/src/core/nginx.c

官方文件亦明確提供 load_module。

來源：
https://nginx.org/en/docs/ngx_core_module.html

這是一個非常重要的 Ckarta 參考：

module identity
→ ABI compatibility
→ unique registration
→ dependency/order
→ module-owned configuration
→ init
→ runtime hooks

而不是「dlopen 後拿一個 function pointer 就算完成」。

## 4. Tomcat

Tomcat 沒有與 Apache/Nginx 完全等價的 generic native DSO module loader。

它的擴充性主要來自 Java class loading、Catalina components、Lifecycle、Listener、Valve、Executor、ProtocolHandler 等 Java 物件／介面。

Bootstrap 建立 class loader，Catalina 再透過 configuration 建立 component object graph。

來源：
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Bootstrap.java
https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/catalina/startup/Catalina.java

因此 Ckarta 不應假設「Tomcat 有 module loader，所以 Ckarta 也只要模仿 Java class loading」。

## 5. Ckarta 建議的 module layers

### Layer 0：core compiled modules

編譯時直接納入 Ckarta。

用途：
- event backend
- HTTP parser
- TLS
- connection
- core routing

優勢：
- 沒有 DSO lookup cost。
- 可以由編譯器直接最佳化。
- ABI 風險最低。

### Layer 1：load-at-start native modules

啟動時由設定載入。

流程：

configuration
→ locate module
→ open shared object
→ validate module descriptor
→ validate ABI version/signature
→ register directives
→ register lifecycle callbacks
→ create module configuration
→ validate configuration
→ initialize
→ freeze

### Layer 2：Java modules

Servlet/container-side 擴充。

由 Java classloader／Servlet container 管理，不進 C module ABI。

## 6. 不建議 request 每次查 module

不要：

request
→ string lookup
→ module registry lookup
→ dynamic symbol resolution
→ handler

應在 startup：

module name
→ stable numeric/indexed registration

request：

pre-resolved handler/context
→ direct function pointer/call

原因不是聲稱 function pointer 一定比其他方法快，而是把昂貴且不可預測的 module discovery 從 hot path 移到 startup。

這和 Nginx cycle/module index 思想一致。

## 7. Module descriptor 應包含的概念

未來 ABI 需要至少研究：

- module identifier
- module ABI version
- host ABI signature
- module type
- required capabilities
- dependency list
- ordering constraints
- directive table
- lifecycle callbacks
- ownership rules
- unloadability
- build/compiler compatibility

目前全部為研究項目，不建立正式 public ABI。

## 8. Unload policy

第一版應採：

load at startup
→ remain loaded until process exit

不要一開始支援任意 runtime unload。

原因：

native module 可能仍有：

- function pointers
- worker thread
- timer callback
- event registration
- allocated objects
- request context
- shared state

若直接卸載 DSO 而其中仍存在任何 callback 或 instruction pointer reference，會形成 use-after-unload 類型的致命問題。

未來 reload 若需要更新 native module，優先考慮新 process／worker generation（新程序／worker 世代）切換，而不是直接 dlclose old DSO。

## 9. 依賴與順序

應支援：

module A requires B
module C must run after A

但不要直接複製 Apache/Nginx 的全部排序語意。

startup 時建立 dependency DAG（相依有向無環圖）：

validate
→ topological order
→ init

cycle 必須直接拒絕。

## 10. 模組安全

native module 等同 executable code。

因此：

- 不允許 request 直接指定 module path。
- 不允許從未信任的 HTTP input 載入模組。
- 允許清單優先於任意搜尋路徑。
- module directory 應具有明確 ownership／permission。
- 載入前驗證 ABI signature。
- 載入後驗證 descriptor。
- 失敗應 fail closed。
- module log 與 error context 必須可追蹤。

Apache 官方文件也警告 DSO module 必須與 major server version 相容；Nginx 則在 ngx_add_module() 直接做 version/signature 檢查。

## 11. 有模組／無模組的效能模型

令：

T_core = 沒有 optional module 的 request processing time

T_dispatch = module handler dispatch cost

T_module = module-specific processing

若 request 不命中模組：

T_unmatched ≈ T_core + ε_registry

其中 ε_registry 必須盡量由 startup pre-resolution 降至接近零。

若命中模組：

T_matched ≈ T_core + T_dispatch + T_module

module existence 的 startup cost：

S_module = parse + load + ABI check + config init + module init

因此 benchmark 必須至少比較：

A. module not built
B. module built but not loaded
C. module loaded but route unmatched
D. module loaded and route matched

不能把 C 與 D 混成「有模組」一組。

## 12. 理論性能結論

若模組 registry 在每 request 執行：

lookup + branch + indirect call + module context access

則所有 request 都承擔額外 hot-path cost。

若 startup 把 route/module resolution 預先編譯成：

route → immutable handler reference

則未命中 optional module 的 request 可以維持近似 core path。

但「接近零」只能作設計目標，實際 CPU cycles、branch prediction、instruction cache 與 data cache 影響必須 benchmark。

## 13. 學術依據

Welsh、Culler、Brewer 的 SEDA 研究支持把不同責任切成明確 stage 與 queue，並以 overload control 管理資源。

DOI：
https://doi.org/10.1145/502059.502057

Hicks、Moore、Nettles 的 Dynamic Software Updating 研究證明 native dynamic updating 涉及 code/data/type consistency，而不是單純替換一個 code pointer。

DOI：
https://doi.org/10.1145/378795.378798

Neamtiu、Hicks、Stoyle、Oriol 的 Practical Dynamic Software Updating for C 進一步研究 C 語言 dynamic update 的安全與一致性問題。

DOI：
https://doi.org/10.1145/1133255.1133991

因此 Ckarta 暫不允許 runtime native module unload／replacement。

## 14. 決策

正式未做列表：

M0 module descriptor／ABI contract
M1 startup module registry
M2 dependency and ordering
M3 module-owned configuration
M4 load-at-start DSO loader
M5 request route pre-resolution
M6 module lifecycle and shutdown
M7 module permission/signature policy
M8 module benchmark matrix

CGI/FastCGI 只在這套 module boundary 上實作，仍不進 core。

本文件不代表上述功能已存在。
