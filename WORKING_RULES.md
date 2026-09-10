# Ckarta 工作準則

本文件是 Ckarta 自動化與人工開發的最高優先工程基線。它定義「如何工作、如何決策、如何驗證」；不取代各專題的 canonical design/research 文件，也不保存會快速過時的目前實作狀態。

每次工作開始前，必須閱讀本文件，以及受本次工作影響的 canonical 文件；至少包括 `docs/ARCHITECTURE.md`、`docs/HOT_PATH_REVIEW.md`、`docs/FUNCTION_TRACE.md`、`docs/CONNECTION_OWNERSHIP.md`。若涉及對應領域，還必須閱讀 `docs/THREAD_MODEL.md`、`docs/CONCURRENCY_MODEL.md`、`docs/JNI_ABI.md`、`docs/CANCELLATION_MODEL.md`、`docs/ERROR_STATE_MATRIX.md`、`docs/SECURITY_BASELINE.md` 與相關 research 文件。

## 1. 規範性用語與規則層級

本文件使用以下規範強度：

- **MUST／必須**：除非本文件明確定義的例外成立，否則不得偏離。
- **MUST NOT／不得**：禁止。
- **SHOULD／應**：一般應遵循；偏離時必須具有可說明的工程理由，且不得破壞 MUST/MUST NOT。
- **SHOULD NOT／不應**：一般不採用；偏離時必須有工程理由。
- **MAY／可以**：允許，但不構成要求。

每條規則應屬於下列至少一種性質：**normative requirement（規範要求）**、**architecture invariant（架構不變條件）**、**engineering policy（工程政策）**、**preference（偏好）**或**procedure（工作程序）**。偏好不得偽裝成規範要求；目前狀態不得偽裝成永久 invariant。

新增規則前必須先檢查能否由既有規則整併；不得因單一偶發錯誤建立只描述該事件的過度具體規則。

## 2. Authority、適用範圍與 Single Source of Truth

發生衝突時，先依「問題所屬領域」選擇適用的 normative authority，而不是把所有來源視為單一全球 total ordering。

目前 repository-local canonical authority 如下：

| 領域 | Canonical authority |
| --- | --- |
| 工作程序與規則 | `WORKING_RULES.md` |
| 目前工程狀態 | `docs/WORK_STATE.md` |
| overall architecture | `docs/ARCHITECTURE.md` |
| connection／memory ownership | `docs/CONNECTION_OWNERSHIP.md` |
| concurrency／thread roles | `docs/CONCURRENCY_MODEL.md`、`docs/THREAD_MODEL.md` |
| JNI ABI | `docs/JNI_ABI.md` |
| cancellation | `docs/CANCELLATION_MODEL.md` |
| error/state transition | `docs/ERROR_STATE_MATRIX.md`、`docs/EXCEPTION_HANDLING_RESEARCH.md` |
| security | `docs/SECURITY_BASELINE.md` |
| HTTP semantics | 適用 RFC 與 `docs/HTTP_FRAMING_POLICY.md` |
| Servlet semantics | Jakarta Servlet 6.1 specification/API |
| Java language semantics | 對應 JLS |
| JVM semantics | 對應 JVMS |
| JNI semantics | 對應版本的 JNI Specification |
| C language semantics | 本文件指定的 ISO C baseline |
| platform API | 對應 OS／platform 官方文件與正式標頭/UAPI |
| Nginx reference | `docs/REFERENCE_SOURCES.md` 固定 revision |
| Tomcat reference | `docs/REFERENCE_SOURCES.md` 固定 revision |
| academic evidence | 各專題唯一的 canonical research document |

同一工程問題不得在多份 repository 文件建立互相獨立、可能分叉的第二套完整規則。其他文件只保留必要結論、限制、適用範圍與 canonical reference。

「規格」「官方 API」「reference implementation」「學術證據」「目前實作」的性質不得混淆。reference implementation 不得取代 normative specification；研究結果不得自動變成架構 invariant；目前實作狀態不得當成規格要求。

## 3. 文件修改、精簡、衝突與追溯

文件修改必須採「保留後整合」原則：先保留所有仍有效的規範、證據、限制、來源與決策，再做增補或受控整併。

每次修訂工作準則、程式碼或文件時，必須進行精簡與整合檢查：辨識重複、冗餘、過時、被更高優先規格取代或可由同一權威來源統一表述的內容；能安全合併就合併、能安全移出就移出、能安全刪除才刪除。不得為縮短文字犧牲有效規範、證據鏈、限制、可追溯性或實作語意。

把目前狀態、版本、branch registry、gate status 或研究細節移出本文件時，必須先確認其 canonical destination 存在且內容可供後續工作取得；不得以「精簡」為由造成資訊遺失。

任何新增或整併規則若產生邏輯衝突，必須先依優先順序、適用範圍與目的嘗試合併。若無法在不造成歧義、互斥或破壞既有高優先要求的情況下解決，不得強行選擇任一版本，必須停止該規則變更並說明衝突雙方、嘗試方式、原因與影響範圍。

刪除或合併重要內容時，必須在 commit message 或受影響文件中留下可追溯理由；完整歷史由 Git commit／PR／branch provenance 保存，不得在 WORK_STATE 重複建立 Git changelog。

## 4. 工作前後同步與持久化

預設後續 Codex 工作可能不了解上一工作階段的未持久化狀態，因此新的工作階段 MUST 以 repository 中現有的規則、canonical 文件、程式碼、測試、Git provenance 與研究紀錄重新建立現況，不得假設對話記憶是權威來源。

每次寫入任何 repository 檔案前，必須重新取得目標檔案最新內容及版本識別；檢查同一路徑、相關文件、branch／PR、CI 與 upstream reference 是否已有更新。若版本不一致、發現未知變更、無法確認寫入基礎或存在競合，不得直接覆蓋，必須重新同步後整合。

每次寫入後必須檢查實際 diff、commit 結果與受影響文件一致性；多檔案變更必須檢查彼此的引用、規則、索引、ABI、lifecycle 與 implementation description。

若工作結果或研究可能改變任何 MD 的規範、證據、限制、決策、流程、索引或實作一致性描述，必須檢查所有受影響的 canonical MD，必要時同步更新；不得因工作表面上只是程式碼修改而跳過文件一致性檢查。

尚未落實成程式碼、測試或正式 MD 的重要研究結果、決策、待辦、限制、驗證狀態或中間成果，應及時以適當且可追溯的形式持久化到 repository。不能證明已持久化的資訊不得被當成跨對話現況。

若本機環境無法網路連線，必須先嘗試本機來源、已下載 source/dependency、Git metadata、既有測試資產、快取或其他不依賴即時網路的方法；若仍無法完成，必須說明實際限制，不得以推測冒充已驗證結果。

## 5. C/Java 邊界與專案目標

Ckarta 是以 Jakarta Servlet 6.1 為相容性目標的 Servlet container 與 Web server。是否宣稱相容以 Servlet 6.1 TCK／適用規格要求為準；未通過 TCK 前不得宣稱正式相容。

C 負責 native data plane：socket、event loop、I/O multiplexing、HTTP parsing/framing、connection management、native buffering、output I/O、platform integration，以及經 architecture 文件明確批准的其他低階資料平面功能。

Java 負責 Servlet API、ServletContext、Request/Response 語意、Filter、Listener、RequestDispatcher、AsyncContext、Session、web application lifecycle、class loading、deployment 與 application execution。

C event-loop／native I/O worker thread MUST NOT 執行 Servlet application code；Servlet application code MUST NOT 依賴 C event-loop thread 執行。

Java/Native 邊界不得把 C HTTP／connection implementation details 直接變成 Servlet-visible semantics；需要對 Servlet 應用程式可見的行為必須依 Servlet specification 定義。

## 6. Normative source policy

### 6.1 HTTP

HTTP framing、Content-Length、Transfer-Encoding、chunked encoding、重複或衝突標頭、message length、request smuggling 等，必須依適用 RFC 與 `docs/HTTP_FRAMING_POLICY.md`。

若不同 implementation reference 做法不一致，不能因「Nginx/Tomcat 如此實作」就繞過適用 RFC。

### 6.2 Servlet

Servlet externally observable semantics 以 Jakarta Servlet 6.1 specification/API 為準。Tomcat 只作 implementation reference；Nginx 只作 native networking/buffering/event architecture reference。

涉及 `ServletInputStream`、`ReadListener`、async dispatch、EOF、non-blocking read readiness、callback sequencing、request lifecycle 或 cancellation 時，修改前必須直接核對對應版本 Servlet specification/API，不得只依 Tomcat source 或記憶推定。

### 6.3 Java

Java 語言語意以指定 JLS 為準；JVM execution/linking/class file/runtime semantics 以指定 JVMS 為準；Java API 行為以對應版本正式 API specification 為準。

Java style convention 僅規範可讀性與一致性，不得用 style convention 解釋 language semantics。

### 6.4 JNI

JNI 語意以對應 JDK/JNI Specification 為準。任何 `JNIEnv*`、reference、exception、thread attachment、direct buffer、native method registration、callback 或 invocation 行為，都必須依該版本規格確認。

## 7. C 語言與編譯器基線

目前 Ckarta portable C language baseline 為 **ISO C11**。目前 CI build contract 以 `-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -pthread` 為基線；compiler version 是驗證環境的一部分，不得讓 compiler default dialect 決定專案 language standard。

GCC 版本升級時不得因此自動升級 C language baseline。C23（ISO/IEC 9899:2024）可以另行研究與評估，但在有明確 architecture／portability decision、相應 compiler/CI coverage 與 migration evidence 前，不得把 C23 作為現行 baseline。

任何 GCC extension、POSIX、Linux UAPI、Windows API、JNI 或其他非 ISO C facility，必須視為額外 platform/API contract；portable core 不得假裝它們屬於 ISO C11。

C code MUST NOT rely on undefined behavior。特別必須檢查 signed overflow、invalid shift、out-of-bounds access/pointer arithmetic、invalid dereference、misalignment、uninitialized value、invalid function pointer call、object lifetime violation 與 data race。

implementation-defined 或 unspecified behavior 若被使用，必須知道其實際語意並證明 deployment/compiler contract 可接受；不可因在目前 GCC 上「看起來正常」就視為 portable behavior。

外部長度、offset、count、capacity、allocation size、multiplication/addition/alignment rounding 與 signed/unsigned conversion，都必須在運算及轉換前後檢查可表示範圍與錯誤條件。不得以 cast 掩蓋可能的截斷、符號改變、alignment 或 range violation。

## 8. C 記憶體、ownership 與生命週期

新增或修改任何變數、欄位、狀態、counter、pointer/reference、handle、buffer reference 或其他可變資料前，必須檢查：

- scope
- owner
- initialization
- lifetime
- valid range
- mutability
- cross-thread publication
- invalidation
- cleanup responsibility

並沿所有成功、錯誤、取消、timeout、disconnect、shutdown 路徑確認不存在未初始化使用、use-after-free、double free、leak、dangling reference、stale state 或 owner lifetime 超界。

若生命週期或作用不清楚，不得先新增資料再靠後續補救；必須先重新設計 ownership/scope。

每個 native allocation 必須能指出 owner、length、capacity、valid lifetime、release rule。pool 不得擁有 Java heap object。

Native buffer 若由 Java 透過 DirectByteBuffer 觀察，C MUST 保證 buffer 在所有 Java 觀察期間仍有效；Java 不得保存已銷毀 native storage 的 reference。

request-scoped temporary data SHOULD 使用 C memory pool，但 pool lifetime 必須有明確 owner；大型資料不得因方便而無限制複製進 Java heap。

zero-copy 是條件式最佳化，不是 correctness guarantee。TLS、compression、Java-generated content 或其他資料轉換可能需要額外 CPU processing／copy。

## 9. C concurrency 與 memory model

預設 concurrency model 為 worker ownership + event loop；Java Servlet execution 使用 executor／thread pool。優先資料 ownership、sharding、immutable state 與單向 handoff，不因「無鎖」名稱就預設採用複雜 lock-free data structure。

每一個 shared mutable object 必須能明確指出：

- writer/reader
- owner
- synchronization mechanism
- publication rule
- lifetime
- teardown synchronization

C11 `volatile` 不得被當成 thread synchronization primitive。需要跨 thread synchronization 時，必須使用適當的 mutex/condition/atomic/other synchronization primitive。

C11 atomic 的 memory ordering 必須有語義上的理由；`relaxed`、`acquire`、`release`、`acq_rel`、`seq_cst` 不得只因「看起來快」而選擇。atomic object lifetime 必須覆蓋所有可能 access。

不得在 shared mutable state 上依賴 data race。任何 synchronization change 都必須重新檢查 publication、reclamation、shutdown 與 error paths。

若採用 lock-free/non-blocking algorithm，除語意正確外，還必須證明 memory reclamation、linearizability（或該演算法適用的 correctness property）、contention profile 與可重現 benchmark benefit。

## 10. 函式、型態、callback 與 ABI 契約

任何新增或引用 C/Java 函式時，必須核對宣告、定義、完整參數型別與順序、回傳/輸出語意、可見範圍與所有 repository 引用位置。

第三方/standard API 必須核對對應版本正式宣告/原始碼，並確認 precondition、postcondition、error contract、ownership、lifetime、reentrancy 與 blocking 假設。

function pointer、callback、JNI method ID、Java method reference、listener、completion callback 與其他間接呼叫，同樣受本規則約束。

修改、移除或重新命名函式時，必須搜尋並檢查完整上下游契約，不得只修第一個 compiler error。

型態轉換必須檢查 range、signedness、width、alignment、truncation、pointer validity、ownership 與 downstream preconditions。不能證明安全時，不得以 cast 掩蓋。

ABI、wire format、request descriptor、completion record 等穩定資料結構，修改時必須更新其 canonical ABI 文件與所有 producer/consumer/test。

## 11. JNI 工程規則

JNI crossing SHOULD 粗粒度化；不得為單一 byte、單一 header 或極小片段反覆進入 Java。

`JNIEnv*` 是 thread-local；不得跨執行緒共享。native thread 必須依 JNI contract 管理 attach/detach。

禁止把 C request struct 逐欄物件化成大量 Java fields/Strings/header objects。預設優先使用 bounded descriptor、opaque handle、薄 Java facade 與 DirectByteBuffer data view；只有需要應用程式可見 semantics 時才建立對應 Java objects。

`Call*MethodA/V`、`NewObjectA/V` 等 JNI crossing 應維持在明確、可審查的 lifecycle boundary；String/Array objectification SHOULD 延遲。

`GetPrimitiveArrayCritical` 不得當作一般 zero-copy 策略。

JNI exception state、Throwable reference、GlobalRef/LocalRef、direct buffer、native allocation、callback reentrancy、shutdown 與 cancellation 必須沿成功及失敗路徑檢查；不得以無條件 `ExceptionClear()` 掩蓋錯誤，也不得以 `ExceptionDescribe()` 取代正式錯誤傳輸。

## 12. C/Java 非阻塞與 thread boundary

C event loop MUST NOT 執行未知時間或不可接受的 blocking operation。需要阻塞的作業必須移入合適的 worker/executor、使用明確 asynchronous primitive，或在 architecture document 證明其不會阻塞。

C event worker 只推進 native state machine、platform I/O 與 connection ownership；JNI bridge 只負責受控 dispatch/completion handoff；Java executor 負責 application-visible Servlet semantics。

任何 native event backend optimization MUST NOT 改變 request/response、ownership、JNI、Servlet 或 cancellation semantics。

## 13. HTTP、connection、buffer 與 lifecycle

HTTP/1.1 framing 必須只有一套 canonical parsing semantics。不同 parser、proxy parser、upstream parser 不得採互相衝突的 message-length rules。

每個 connection 必須有顯式 state machine，以及 bounded timeout 與 resource limit。`FINISHED`、`DRAINED`、keep-alive recycle、terminal close 等不同 lifecycle boundary 不得混為一談。

request body producer/consumer 若使用 bounded queue，必須定義 queue capacity、full behavior、partial-write behavior、ownership、backpressure、EOF、error、cancel 與 shutdown semantics。需要 transactional acknowledgement 時，必須避免 rejected input 被重新 feed 而重播 parser state。

pipeline preservation 必須是明確 invariant：處理當前 request 後，屬於下一 request 的 leftover bytes 不得遺失、重複消費或被錯誤歸屬。

HTTP request smuggling 是 blocking security requirement，不是 optional optimization。

## 14. Servlet 6.1 語意與相容性

Servlet 6.1 compatibility work MUST 以 specification/API behavior 為第一依據，Tomcat implementation 只作 cross-check。

涉及 `ServletInputStream`/`ReadListener` 等 non-blocking API 時，必須明確定義並測試 `isFinished()`、`isReady()`、`setReadListener()`、`read()`、`read(byte[],...)`、Servlet 6.1 `read(ByteBuffer)`、`onDataAvailable()`、`onAllDataRead()`、`onError()` 及其 illegal-call、EOF、callback sequencing、reentrancy、backpressure、error、cancel 與 shutdown semantics。

不得因 native queue 的「有資料」就直接假設 Servlet `isReady()` 必須為 true；Servlet-visible readiness 必須由 canonical Servlet state machine 決定。

未通過 TCK 前不得把測試通過 smoke slice、Tomcat comparison 或部分 API coverage 稱為「Servlet 6.1 相容」。

## 15. Error、completion、cancellation 與 retry

exception、error status、HTTP status、cancellation、timeout、client disconnect 與 process-fatal condition 不得混成單一 error channel；各層必須有唯一主要 error authority，並定義 propagation、precedence、terminal transition 與 recovery。

所有 asynchronous error/completion/cancellation path 必須定義 exactly-once terminal outcome，以及 late completion、duplicate completion、owner teardown、shutdown、timeout、cancel race 的優先序。不得依 callback arrival order 或未定義 race 推測結果。

清理責任必須與 request/connection/buffer owner 綁定；error path 不得產生 UAF、double free、leak 或已失效 owner 上的 completion。

client-visible error 與 internal diagnostic 必須分離。外部回應不得預設暴露 stack trace、server/build version、filesystem path、native pointer、credentials、TLS secret 或其他內部實作資訊。log 不得直接拼接未驗證外部輸入而造成 injection。

retry 不得由 exception 單獨觸發。任何 retry 必須先證明 operation semantics、idempotency、request replayability、bytes-sent state、timeout budget、upstream state 與 cancellation state 允許；非冪等 request 不得因一般 exception/error 自動 retry。

新增 error category/status/exception translation/fatal path/recovery transition 時，必須同步檢查 `docs/EXCEPTION_HANDLING_RESEARCH.md`、`docs/ERROR_STATE_MATRIX.md` 及受影響 architecture/JNI/lifecycle/security/test 文件。

## 16. Thread、shutdown 與程序入口

正式產品程序唯一外部入口為 C `main()`；Java `main()` 僅可用於測試或工具。

C main/control plane 擁有 process lifecycle、native configuration、native resources、listener/socket、worker 與 JVM bootstrap coordination。JVM 啟動由 C 透過 JNI Invocation API 依明確 startup state machine 管理。

第一階段禁止啟動 JVM 後 fork 並讓子程序繼承已建立 JVM。

shutdown 必須是有序 state transition，而非「各 thread 自行退出」。必須明確定義停止新工作、停止 dispatch、排空／取消 queue、完成或取消 Java task、connection close、worker stop、JNI detach、JVM termination 與 native cleanup 的順序及 ownership。

## 17. Memory、zero-copy、static/proxy/session/security baseline

request-scoped memory 優先短生命週期與 bounded allocation。任何 external length/offset/count 必須做 overflow/range validation。

static file serving 預設可繞過 JVM，但必須防 path traversal、symlink escape、canonicalization mismatch、range abuse 與資源耗盡；不得無條件整個檔案載入 heap。

reverse proxy／load balancing 設計至少必須明確考慮 weighted round robin、failure counting、timeout、connection limit、backup server 與 upstream connection reuse。不得未分析 method semantics、replayability 與 sent-byte state 就 retry non-idempotent request。

Session semantics 由 Java Servlet container 管理；C 不得建立與 Java Session lifecycle 競爭的第二套 Servlet Session authority。

安全 baseline 至少涵蓋 TLS、HTTP security headers、request size limits、rate/connection limits、timeouts、access control、request smuggling、Slowloris、buffer/integer overflow、UAF、double free 與 least privilege。

所有 parser SHOULD 使用 pointer + length 或等價的明確 bounded representation。禁止 `gets`、`strcpy`、`strcat` 與無界 `sprintf` 類用法。

## 18. Platform API、Linux 與 io_uring

平台特定 documented API 必須集中在明確 platform backend；portable core 不得散落 platform-specific conditionals。

新增/修改 platform API 呼叫時，必須核對對應版本官方文件、標頭/UAPI、完整參數與回傳契約、錯誤語意、descriptor/handle/OVERLAPPED ownership、lifetime、cancel、timeout 與 shutdown。

Linux 優先使用 libc 或正式 system-call wrapper；Windows 優先使用 documented Win32/Winsock API。不得以 hard-coded raw syscall number、未文件化 NT Native API 或不穩定 internal kernel interface 作一般 runtime ABI。

io_uring 是受控 Linux backend exception：可以在 platform backend 中使用 `<linux/io_uring.h>` UAPI、libc `syscall()` wrapper 與 kernel documented `io_uring_setup`、`io_uring_enter`、`io_uring_register`；不得硬編碼 syscall number，不得把 liburing 變成核心 runtime dependency，也不得複製 library-private implementation 作 Ckarta ABI。

io_uring activation MUST 由 runtime probe 決定，不得只以 kernel version 判定。至少核對 ring setup、required opcode support 與必要 feature flags；被 kernel/container/seccomp/security policy 拒絕時必須可回退到既有 backend。initial backend 不得把 SQPOLL、IOPOLL、ZCRX 等尚未驗證的 advanced facility 當成必要條件。

kernel version 是 deployment/research hint，不是 capability proof。

## 19. Nginx、Tomcat、第三方程式碼與 license

Nginx/Tomcat 是 reference implementations，不是 Servlet/HTTP/C language authority。固定 revision、用途與來源由 `docs/REFERENCE_SOURCES.md` 管理。

禁止未經架構決策直接複製 upstream code。任何移植、複製或衍生程式碼前，必須檢查 license、copyright、dependency、platform assumptions、安全與 semantics 差異。

`third_party` submodule 預設唯讀。若 upstream source 確實需要 patch，必須保存原因、upstream revision、可重現 patch、license/copyright impact 與驗證測試。

研究不得以「某行看起來相似」直接推導 semantic equivalence；必須檢查上下游資料流、state transition、ownership、error semantics 與 observable behavior。

## 20. Research、evidence 與學術來源

任何研究結論必須區分：

- normative requirement
- verified implementation fact
- measured result
- design inference
- hypothesis/open question

不得把 inference 寫成 verified fact，也不得把 benchmark result 外推成未測 workload。

任何 academic source 必須確認作者、標題、出版資訊、venue、DOI 或 stable URL，以及可閱讀位置。優先 peer-reviewed publication；preprint 必須明確標記為 preprint，不得與 peer-reviewed evidence 混同。

引用原文時必須逐字核對；paraphrase 必須能指出所依據的 source passage。非中文原文若引用，必須同時提供核實過的翻譯。

重大 architecture/performance/safety decision SHOULD 交叉比對適用規格、官方 source、固定 Nginx/Tomcat/OpenJDK references 與相關 academic evidence；不能由單一 secondary source 推導結論。

## 21. Testing、verification gates 與 compatibility claims

測試必須依修改風險選擇合適 gate，而不是只追求測試數量。

最低驗證層級：

1. compile/build
2. unit
3. integration
4. concurrency/race/lifecycle
5. sanitizer
6. fuzz/negative/resource-exhaustion
7. TCK/compatibility
8. reproducible benchmark

不同變更至少應達到與其風險相符的 gate，例如 parser 變更需包含 negative/fuzz；ownership/JNI/lifecycle 變更需包含 lifetime/concurrency/cleanup coverage；backend 變更需有等價 workload integration；performance claim 必須有 benchmark。

GitHub Actions 是最新 CI truth source。CI 未完成、被取消或存在不確定結果時，不得標示該 gate 已通過。

compatibility claim 必須以正式 compatibility test/TCK 或明確規範允許的驗證方式為準；smoke test 不等於 compatibility certification。

若 verification environment 未固定，結果只能稱為該環境的 measurement；重要 benchmark/compatibility evidence 必須記錄 Ckarta commit、OS、kernel、CPU、compiler exact version、JDK exact version、TLS、concurrency、workload、request/response size、keep-alive、cache state、build flags 與相關 backend configuration。

epoll 與 io_uring 比較還必須記錄 kernel exact release、runtime probe result、required features/opcodes、ring entries、SQ/CQ configuration、SQPOLL/其他 special flags、registered/provided buffers、CPU affinity 與 fallback status；不同 capability/fallback 狀態不得直接比較。

## 22. CI 與 reproducibility policy

CI environment 是工程 contract 的一部分。不得依賴 moving compiler default、moving Java default、moving OS image 或未固定 dependency 產生無法解釋的 semantics 差異。

workflow SHOULD 固定主要 OS image label，並在 verification log 中輸出 `java -version`、`javac -version`、compiler version、kernel release 與重要 dependency/version information。

build flags 必須明確指定 language level；不得因 compiler upgrade 而依賴 implicit default dialect。

可重現建置需要的 dependency artifact 應使用版本固定與 checksum/hash verification；不得以未驗證下載內容替代已宣稱的版本。

## 23. Branch、main、PR 與工作守則來源

若工作可以直接安全完成在 `main`，優先直接使用 `main`；不得為形式上的隔離建立不必要 branch。

若必須建立 branch，建立前必須盤點 active branch/PR，檢查其 WORK_STATE、程式碼、研究、測試、CI、設定與其他相關 repository 內容，並以此作為 diff、精簡與整併 baseline。

`main/WORKING_RULES.md` 是唯一有效的工作守則來源。非 main branch 若存在 WORKING_RULES.md，不得視為該 branch 的執行準則；應移除，若需歷史比對只能讀取並抽取有效規則。

非 main branch 的 WORK_STATE 只代表該 branch 自身狀態，不得覆蓋 main canonical state。branch 整併時只能抽取仍有效且尚未存在於 main canonical docs 的事實、決策與限制。

每次建立、重新啟用、修改或準備關閉 branch 時，必須更新 main `docs/WORK_STATE.md` 的 branch status registry；branch 自身的 WORK_STATE 也必須保持與實際 GitHub branch/PR 狀態一致。文件與 Git 實際狀態不一致時，先查 Git，再修文件。

branch 結束、merged、superseded 或不再需要時，應刪除／關閉；若權限不允許刪除，至少關閉待合併狀態並記錄 superseded/merged reason。

若 `main` 因本次變更而變紅，不得繼續無關堆疊功能；應優先恢復最新 verification gate，除非有明確安全事件或其他更高優先工作。

## 24. Rule-change control、例外與 emergency path

工作過程若發現 bug、缺陷、未定義行為、不安全行為、lifecycle 漏洞或設計矛盾，先分析實際原因與影響，確認現有規則是否已適用；能由現有規則處理就修正實作，不因單一事件新增規則。

若現有規則不足，新增規則前必須回答：

1. 防止哪一類可泛化風險？
2. 現有規則為何不足？
3. 為何不能合併到既有規則？
4. 能否透過 code/test/CI 機械驗證？
5. 是否應下放至專題 canonical document？

規則不得因一次偶發 failure 而過度具體化，除非該事件揭示可普遍預防的工程風險。

任何規則例外必須是 explicit、bounded、traceable 的 temporary deviation；至少記錄理由、範圍、owner、有效條件、補償性驗證、移除條件與追蹤位置。例外不得偷偷變成新的永久 authority。

若工作本身正是修訂 `WORKING_RULES.md`，仍必須先遵守修訂前版本的保留後整合、衝突處理、最新內容同步、來源核對與寫後一致性檢查；新版本只有在正式 commit 後才成為後續工作準則。

## 25. 文件索引與 canonical routing

長篇研究只保留一個 canonical version。工作守則只保存跨專題且長期有效的工作政策與 invariants；研究細節、版本清單、目前 gate、state machine、benchmark protocol 與 implementation status 應路由到對應 canonical docs。

核心 canonical 文件至少包括：

- `docs/ARCHITECTURE.md`
- `docs/HOT_PATH_REVIEW.md`
- `docs/FUNCTION_TRACE.md`
- `docs/CONNECTION_OWNERSHIP.md`
- `docs/CONCURRENCY_MODEL.md`
- `docs/THREAD_MODEL.md`
- `docs/JNI_ABI.md`
- `docs/JNI_COST_MODEL.md`
- `docs/CANCELLATION_MODEL.md`
- `docs/ERROR_STATE_MATRIX.md`
- `docs/EXCEPTION_HANDLING_RESEARCH.md`
- `docs/HTTP_FRAMING_POLICY.md`
- `docs/SECURITY_BASELINE.md`
- `docs/REFERENCE_SOURCES.md`
- `docs/WORK_STATE.md`

修改本文件後，必須再次檢查 README、architecture、hot path、JNI、lifecycle、HTTP、concurrency、security、test、reference source 與 WORK_STATE 文件的一致性，並確認沒有因精簡而刪掉仍有效的規範、證據或限制。
