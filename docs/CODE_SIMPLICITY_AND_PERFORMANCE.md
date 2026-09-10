# Ckarta 程式碼精簡與效能工程政策

本文件是 WORKING_RULES.md 下的 canonical engineering policy，規範「如何讓 Codex 產生較精簡、較容易驗證、且較可能保留效能的程式碼」。它不是 benchmark 結果，也不取代 C/Java/JNI/HTTP/Servlet 的 normative authority。

## 1. 核心原則

Ckarta SHOULD prefer the simplest implementation that preserves correctness, ownership, lifecycle, portability boundary、observable semantics 與可驗證性。

「source code 較少」不得直接等同於「runtime 較快」。精簡的主要目標是降低不必要的狀態、分支、抽象層、複製與維護成本；效能結論必須由 compiler/JIT analysis、profile 或 benchmark 支持。

熱路徑優先減少 runtime work，而不是機械式 code golfing。尤其應優先檢查：

- 不必要的 memory allocation/free。
- 不必要的 data copy／materialization。
- 不必要的 JNI crossing、Java objectification 與 thread handoff。
- 不必要的 lock acquisition、shared-state contention 與 cache-unfriendly ownership transfer。
- 不必要的 syscall、event registration、wake-up 與 kernel/user transition。
- 可以由編譯器/JIT 自動消除而被手工重複實作的 wrapper、cache 或 micro-optimization。

## 2. C/GCC

目前 C11 + `-O2` 已讓 GCC 啟用大量 inline、interprocedural、loop/vectorization 與其他最佳化；因此不得為了「看起來更快」而任意增加 `inline`、手工展開、複製函式或複製資料結構。GCC 的 heuristic、LTO 與 profile-guided optimization 應被視為可能已經處理其中一部分工作。

LTO、PGO 或較激進 compiler option 只有在實際 workload、compiler version、binary 與 benchmark evidence 顯示收益，且不破壞 portability／debuggability／可重現性時才採用。

error path、shutdown path、ownership transfer 與 security-sensitive validation 不得為減少幾行程式碼而壓縮成較難驗證的形式。

## 3. Java/HotSpot

Java source 層級的 allocation、wrapper object 與小函式不應僅因「有 allocation／有 method call」就手工消除。HotSpot 可能透過 inlining、escape analysis、scalar replacement 等最佳化移除部分物件與呼叫成本。

真正應避免的是會阻礙 JIT 分析或造成不可避免 runtime work 的設計，例如過早 materialize 大量 String/header object、跨 thread 發布不必要的物件、細粒度 JNI crossing 或 native/Java 之間反覆複製資料。

## 4. JNI 與資料邊界

JNI hot path SHOULD 採批次語意邊界：request-level dispatch、compact metadata、bounded native storage 與 bulk byte view 優先於逐 header／逐 byte／逐 field crossing。

DirectByteBuffer 可作 native memory view，但不得把「少一次 copy」誤當成 lifetime correctness；native ownership 必須先於 performance optimization 定義。

## 5. 抽象與重複程式碼

抽象層若能隔離 platform ABI、ownership 或語意責任，應保留；不得僅因 source line count 增加就刪除必要 abstraction。

反向地，純轉發且不提供 contract、ownership、error translation、instrumentation 或 compiler/JIT optimization value 的 wrapper，SHOULD 優先考慮移除或合併。

不得機械式要求「零 duplication」。實證研究顯示 duplicate code 的維護影響取決於情境，因此應依 semantic ownership、演化頻率與 platform separation 判斷，而不是套用單一 blanket rule。

## 6. Codex 產碼規則

Codex 產生程式碼時，預設採以下順序：

1. 先建立最小可證明的 ownership／state／ABI contract。
2. 再以最少必要資料結構實作該 contract。
3. 優先重用已存在且語意相同的 helper；不得為一次性呼叫新增抽象層。
4. 熱路徑才研究 micro-optimization；cold/error path 優先保持直接與可審查。
5. 一旦需要新增 abstraction，必須指出它隔離哪個 ABI、ownership、lifecycle 或可驗證風險。
6. 一旦聲稱「更快」，必須給出 compiler/JIT reasoning 或實測 benchmark；沒有證據時只能稱為 design hypothesis。

## 7. 與 platform portability 的關係

精簡不得把 POSIX `fd`、Windows `SOCKET`、Win32 `HANDLE` 或其他 native type 洩漏到不應知道它們的核心層，因為這會把後續 platform branching、error translation 與 lifetime complexity 擴散到整個 codebase。

目前 Win32 work 的優先目標不是建立 POSIX facade，而是保持 Ckarta 自有 semantic contract，讓 Linux/epoll 與未來 Windows/IOCP backend 各自使用原生 API。

## 8. 研究依據

GCC 官方 Optimize Options 說明 `-O2` 已啟用多項 inlining、IPA、loop/vectorization 等最佳化；LTO 與 profile feedback 可進一步把跨 translation unit 與 profile information 用於最佳化。這支持「讓 compiler 做它擅長的工作，不要以 source-level code golf 取代 compiler optimization」的原則。

OpenJDK HotSpot escape-analysis research 說明 escape analysis 與 scalar replacement 可消除部分 allocation／field access 成本；Java JIT inlining 的 empirical study 也顯示 profile-directed inlining 可以改善效能或 compilation overhead。因此 Java source 不應以「每個 object 都必須手工消除」作為固定規則。

軟體工程實證研究則支持以 complexity/static-analysis evidence 聚焦高風險區域，而不是單純追求較短 source；duplicate-code 的研究也不支持一律將 duplication 視為必須消除的缺陷。

主要來源：

- GCC, Using the GNU Compiler Collection, Optimize Options：https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
- OpenJDK HotSpot Escape Analysis and Scalar Replacement Status：https://cr.openjdk.org/~cslucas/escape-analysis/EscapeAnalysis.html
- Suganuma, Yasue, Nakatani, “An Empirical Study of Method In-lining for a Java Just-in-Time Compiler”, Java VM Research and Technology Symposium 2002：https://www.usenix.org/conference/java-vm-02/empirical-study-method-lining-java-just-time-compiler
- Omri, Montag, Sinz, “Static Analysis and Code Complexity Metrics as Early Indicators of Software Defects”, Journal of Software Engineering and Applications 11(4), 153–166 (2018), DOI 10.4236/jsea.2018.114010：https://doi.org/10.4236/jsea.2018.114010
- Hotta et al., “An Empirical Study on the Impact of Duplicate Code”, Advances in Software Engineering (2012), DOI 10.1155/2012/938296：https://doi.org/10.1155/2012/938296
- Microsoft, UCRT Compatibility：https://learn.microsoft.com/en-us/cpp/c-runtime-library/compatibility?view=msvc-170
- libuv `uv-win.h`：https://github.com/rwinlib/libuv/blob/master/include/uv-win.h
