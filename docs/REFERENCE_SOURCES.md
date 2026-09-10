# Ckarta 參考原始碼與規範基線

本文件保存 Ckarta 需要固定版本、正式規格、官方 API、reference implementation 與研究來源的位置。它不是工作守則，也不是目前工程狀態；工作程序由 `WORKING_RULES.md` 定義，目前狀態由 `docs/WORK_STATE.md` 定義。

## 1. Repository-local authority routing

| 領域 | 權威／用途 |
| --- | --- |
| 工作程序與工程政策 | `WORKING_RULES.md` |
| 目前工程狀態與 gate | `docs/WORK_STATE.md` |
| Overall architecture | `docs/ARCHITECTURE.md` |
| Ownership / lifetime | `docs/CONNECTION_OWNERSHIP.md` |
| Concurrency / thread model | `docs/CONCURRENCY_MODEL.md`、`docs/THREAD_MODEL.md` |
| JNI ABI / JNI cost | `docs/JNI_ABI.md`、`docs/JNI_COST_MODEL.md` |
| Cancellation | `docs/CANCELLATION_MODEL.md` |
| Error / state transition | `docs/ERROR_STATE_MATRIX.md`、`docs/EXCEPTION_HANDLING_RESEARCH.md` |
| HTTP framing | `docs/HTTP_FRAMING_POLICY.md` + applicable RFC |
| Security | `docs/SECURITY_BASELINE.md` |
| Servlet semantics | Jakarta Servlet 6.1 specification/API |
| Java language / VM semantics | JLS / JVMS for the selected Java baseline |
| JNI semantics | JNI Specification for the selected JDK |
| C language semantics | ISO C11 for the current project baseline |

## 2. C language and compiler baseline

### ISO C11

Ckarta portable C 的目前 language baseline 為 ISO C11。ISO/IEC 9899:2011 定義 C programming language 的 syntax、constraints、semantics、representation 與 implementation limits。

Official ISO record:
https://www.iso.org/standard/57853.html

Ckarta build system 目前明確使用 `-std=c11`，因此不得依 GCC default language dialect 漂移。

### C23 status

ISO/IEC 9899:2024（C23）已於 2024-10 發布為 ISO C 第 5 版；它是未來可評估的 language baseline，不是目前 Ckarta baseline。

Official ISO record:
https://www.iso.org/standard/82075.html

GCC 15 起 C compilation 的 default 從 `-std=gnu17` 改為 `-std=gnu23`，因此 compiler upgrade 更不能被用來隱式改變 Ckarta language standard。

GCC 15 release notes:
https://gcc.gnu.org/gcc-15/changes.html

GCC C dialect options:
https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html

## 3. Java SE baseline

目前開發與 CI baseline 為 OpenJDK 21；Java source/VM semantics 以 Java SE 21 specification 為準。這是目前 verification baseline，不代表 Servlet 6.1 只能在 Java 21 執行。

Java SE 21 specifications:
https://docs.oracle.com/en/java/javase/21/docs/specs/

JLS 21:
https://docs.oracle.com/javase/specs/jls/se21/html/

JVMS 21:
https://docs.oracle.com/javase/specs/jvms/se21/html/

JNI Specification 21:
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/

## 4. Jakarta Servlet 6.1

Jakarta Servlet 6.1 是 Ckarta 的 compatibility target。官方頁面列出 Servlet 6.1 的 minimum Java SE version 為 17，並提供 specification、API、TCK 與 6.1.0 API artifact。

Official release/specification page:
https://jakarta.ee/specifications/servlet/6.1/

Specification PDF:
https://jakarta.ee/specifications/servlet/6.1/jakarta-servlet-spec-6.1.pdf

Javadocs:
https://jakarta.ee/specifications/servlet/6.1/apidocs/

## 5. 固定 reference implementations

### Nginx

版本：1.30.4 stable

Git tag：`release-1.30.4`

commit：`017cf98dcce217946572a896f0992370475e189f`

repository：
https://github.com/nginx/nginx

官方版本資訊：
https://nginx.org/2026.html

研究用途：event architecture、connection ownership、buffering、nonblocking I/O、request-body handling 與 resource control。Nginx 不作 Servlet semantics authority。

### Apache Tomcat

版本：11.0.25

Git tag：`11.0.25`

commit：`cbe6e15ee81e2fc6232954292a80cca5d1e84009`

repository：
https://github.com/apache/tomcat

官方版本資訊：
https://tomcat.apache.org/tomcat-11.0-doc/

研究用途：Servlet implementation、lifecycle、request/response interaction 與 nonblocking Servlet behavior。Tomcat 不取代 Servlet specification 或 HTTP RFC。

### Apache HTTP Server

版本：2.4.68

tag：`2.4.68`

tagged commit：`736bb657405eb73fd68a64772c3a908807bdb887`

repository：
https://github.com/apache/httpd

官方版本資訊：
https://httpd.apache.org/download

Apache httpd 2.4.68 目前作為外部固定研究來源，不加入 `third_party` submodule。

## 6. Java Servlet API dependency

Jakarta Servlet API：6.1.0
Maven coordinates：`jakarta.servlet:jakarta.servlet-api:6.1.0`

來源：
https://jakarta.ee/specifications/servlet/6.1/
https://central.sonatype.com/artifact/jakarta.servlet/jakarta.servlet-api/6.1.0

Ckarta 目前只使用 API artifact 作為 compile/test boundary，不把它視為 Servlet implementation。版本固定與 checksum verification 由 build system 管理；下載失敗不得以未驗證 artifact 替代。

## 7. Linux io_uring

Official API / UAPI references：
https://man7.org/linux/man-pages/man7/io_uring.7.html
https://man7.org/linux/man-pages/man2/io_uring_setup.2.html
https://man7.org/linux/man-pages/man2/io_uring_enter.2.html
https://kernel.org/doc/html/latest/userspace-api/io_uring.html

Kernel release lifecycle：
https://www.kernel.org/releases.html

Ckarta io_uring research follows runtime capability probing rather than kernel-version-only feature assumptions.

Academic sources：

Matthias Jasny, Muhammad El-Hindi, Tobias Ziegler, Viktor Leis, Carsten Binnig, "High-Performance DBMSs with io_uring: When and How to use it", Proceedings of the VLDB Endowment 19(9), 2317–2330 (2026), DOI 10.14778/3819518.3819553.
https://doi.org/10.14778/3819518.3819553

Constantin Pestka, Marcus Paradies, Matthias Pohl, "Asynchronous I/O -- With Great Power Comes Great Responsibility", arXiv:2411.16254 (preprint).
https://arxiv.org/abs/2411.16254

io_uring academic evidence is architecture/performance context only; Ckarta performance claims require Ckarta-versioned benchmark evidence.

## 8. Event-driven concurrency evidence

Matt Welsh, David Culler, Eric Brewer, "SEDA: An Architecture for Well-Conditioned, Scalable Internet Services", ACM SIGOPS Operating Systems Review 35(5), 230–243 (2001), DOI 10.1145/502059.502057.
https://doi.org/10.1145/502059.502057

SEDA is research evidence for explicit stage boundaries, queues and overload management; Ckarta does not claim to be a SEDA implementation.

Nickolai Zeldovich, Alexander Yip, Frank Dabek, Robert T. Morris, David Mazières, Frans Kaashoek, "Multiprocessor Support for Event-Driven Programs", USENIX Annual Technical Conference (2003), pp. 239–252.
https://www.usenix.org/conference/2003-usenix-annual-technical-conference/multiprocessor-support-event-driven-programs

## 9. Research and reproducibility policy

Research、benchmark 與 compatibility evidence 應記錄至少：

- Ckarta commit
- fixed upstream/reference commits
- compiler exact version
- JDK exact version
- OS / architecture
- kernel exact release
- build flags
- relevant configuration

不得使用未固定 upstream branch 作為正式 benchmark 基線。引用 upstream source 必須指出用途與 revision，不得以一行相似程式碼直接推導 semantic equivalence。

## 10. Third-party source modification

`third_party/nginx` 與 `third_party/tomcat` 預設唯讀。若日後確實需要 patch upstream source，必須記錄原因、upstream revision、可重現 patch、license/copyright impact 與驗證測試。

Ckarta 不因使用 submodule 而取得修改或重新授權第三方程式碼的額外權利；任何移植、複製或衍生程式碼都必須再次檢查適用 license 與 copyright notices。
