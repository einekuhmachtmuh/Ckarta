# OpenJDK 21u HotSpot 版本追溯稽核

本文件是 Ckarta 對「OpenJDK update 版本差異是否足以影響 JNI／thread／Servlet bridge 學術引用」的權威版本稽核。研究不再把「舊 JNI benchmark」與「現代 HotSpot implementation」混為一談；需要 implementation-level 結論時，直接以對應 OpenJDK 21u tag 的 HotSpot source 為證據。

## 1. 固定比較對象

JDK 21.0.8 GA：`jdk-21.0.8-ga`

tag object：`cb85c9f9dd00efac4f435061634cee46a62c3149`

commit：`54f2095960f01d957f2335cafa0defb956e13a2c`

https://github.com/openjdk/jdk21u/tree/jdk-21.0.8-ga

JDK 21.0.11 GA：`jdk-21.0.11-ga`

tag object：`5dedca0392b1303342071efcbc90533396bdf475`

commit：`d8615be992082324aaeb01bd6db275e30485aeea`

https://github.com/openjdk/jdk21u/tree/jdk-21.0.11-ga

版本差異比較：

https://github.com/openjdk/jdk21u/compare/jdk-21.0.8-ga...jdk-21.0.11-ga

JDK 21.0.11 已於 2026-04-21 發布；Oracle release notes 指出該 update 仍符合 Java SE 21 specification，因此此處比較的是同一 Java SE major version 之 update-level implementation 演化，而不是 API major-version 變更。

來源：
https://www.oracle.com/java/technologies/javase/21all-relnotes.html

## 2. 真正會影響本專案結論的 HotSpot 路徑

目前 Ckarta 需要證明的 implementation-level claims 分成四類：

1. JNI method invocation 是否進入 HotSpot `JavaCalls` runtime machinery。
2. native thread attach／detach 是否具有明確的 thread-local `JNIEnv*` 與退出生命週期。
3. `NewDirectByteBuffer` 是否只是建立 Java view，而不是把 native buffer 複製進 Java heap。
4. `GetPrimitiveArrayCritical` 是否能作為一般零拷貝策略。

其中 (2)–(4) 的規範性條件由 Java SE 21 JNI specification 定義；HotSpot source 用來確認實作路徑，而不是取代規範。

## 3. JNI invocation：21.0.8 與 21.0.11 的核心路徑沒有版本漂移

固定兩個 tag 的 `src/hotspot/share/runtime/javaCalls.cpp` blob SHA 均為：

`0ae0d4540e417e92f803eb43671e91cde24ea4fc4`

因此 21.0.8 → 21.0.11 之間，`JavaCallWrapper`、`JavaCalls::call`、`JavaCalls::call_virtual` 這條直接支撐本專案「JNI method call 不是單純 function-pointer jump」的 runtime machinery 並沒有改變。

21.0.8：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/runtime/javaCalls.cpp

21.0.11：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.11-ga/src/hotspot/share/runtime/javaCalls.cpp

固定 21.0.11 `jni.cpp` 的 `jni_invoke_nonstatic` 會解析 receiver 與 `jmethodID`，再呼叫：

`JavaCalls::call(result, method, &java_args, CHECK)`

這與 `JavaCalls.cpp` 所展示的 runtime call machinery 直接銜接。因此「JNI → HotSpot Java call machinery」不是從 current master 倒推，而是由對應 21u tag 直接追溯。

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.11-ga/src/hotspot/share/prims/jni.cpp
https://github.com/openjdk/jdk21u/blob/jdk-21.0.11-ga/src/hotspot/share/runtime/javaCalls.cpp

## 4. 21.0.8 → 21.0.11 的 `jni.cpp` 唯一少量差異不影響本研究結論

GitHub fixed-tag compare 顯示 `src/hotspot/share/prims/jni.cpp` 僅有 6 行 change：5 additions、1 deletion。

可追溯到的 patch 內容包含：

- copyright 年份由 2023 更新至 2025。
- 一處原本為 lint noise 的 `return nullptr;` 被移除，前面的 `default: ShouldNotReachHere();` 後不再保留該不可達 return。

該 patch 不位於 `jni_invoke_nonstatic`、`jni_NewObjectA`、`jni_GetPrimitiveArrayCritical`、`jni_NewDirectByteBuffer` 等本研究關心的 JNI entry 路徑；因此不存在「因 21.0.8 與 21.0.11 在 JNI invocation implementation 上有重大差異，導致舊學術引用無法對應」的情況。

版本 compare：
https://github.com/openjdk/jdk21u/compare/jdk-21.0.8-ga...jdk-21.0.11-ga

固定 21.0.11 source：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.11-ga/src/hotspot/share/prims/jni.cpp

## 5. `NewObjectA/V`：版本不再是目前的不確定因素

固定 JDK 21.0.11 `jni.cpp` 的 `jni_NewObjectA` 路徑先透過 `InstanceKlass::allocate_instance(...)` 配置 Java object，再建立 JNI local reference，最後透過 `jni_invoke_nonstatic(...)` 執行 constructor invocation。

所以 C struct → 多個 Java object／String／header object 的設計成本不能被描述為單純 memory aliasing；至少存在 Java object allocation、JNI handle、constructor invocation 與 Java object semantics。

此處並沒有必要用 JDK 17、JDK 8 的歷史 HotSpot implementation 來推論 JDK 21。

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.11-ga/src/hotspot/share/prims/jni.cpp

## 6. `NewDirectByteBuffer`：規範與 implementation 分工

Ckarta 使用 `NewDirectByteBuffer` 的理由不是「HotSpot 保證零成本」，而是 JNI 直接建立一個 Java `ByteBuffer` view 指向既有 native memory。Java SE 21 JNI specification 同時要求 native memory 在 Java view 使用期間保持有效。

因此目前 ABI 應定義為：

C owns storage
→ JNI creates direct-buffer view
→ Java request facade observes the view
→ C retains lifetime until the Java-side operation is no longer allowed to access it

這是 ownership／lifetime 保證，不是對 CPU cache、GC 或 method invocation 成本的零化承諾。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

## 7. `GetPrimitiveArrayCritical`：不能被本研究改寫成一般零拷貝 API

Java SE 21 JNI specification 明確保留「可直接回傳 pointer 或使用 copy」的實作自由，並對 critical region 施加嚴格限制。因此即使某個 HotSpot build 在某個 array 情況下返回 direct pointer，也不能把它當成 Ckarta request buffer 的一般 ABI。

本專案仍採 `DirectByteBuffer` 作為 native-memory view 的主要模型；`GetPrimitiveArrayCritical` 只保留於必要、短暫且遵守規範限制的場景。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

## 8. Thread attach：規範性結論與 HotSpot implementation 不應混寫

Java SE 21 Invocation API 規定 `JNIEnv*` 只對取得它的 current thread 有效；native thread 要在 JVM 中執行 Java/JNI 工作必須先 attach，而 attached native thread 在終止前應 detach。

因此 A 型 topology 的基本規則可以定稿：

C worker
→ AttachCurrentThread
→ reuse its own JNIEnv*
→ dispatch Java work
→ DetachCurrentThread before thread termination

這個結論不是依賴某個 update 的微小 implementation detail，因此 21.0.8/21.0.11 的差異不會推翻它。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/invocation.html

HotSpot 21.0.8 / 21.0.11 的 `JavaThread` source 也都保留 attached Java-thread exit cleanup 與 `JNI DetachCurrentThread` 相關語意；兩個 tag 的檔案內容有 update-level 差異，但目前沒有證據顯示它改變上述 JNI contract。

21.0.8：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/runtime/javaThread.cpp

21.0.11：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.11-ga/src/hotspot/share/runtime/javaThread.cpp

## 9. 舊學術 JNI benchmark 現在可以如何正式使用

Kurzyniec／Sunderam 的 JNI benchmark、以及更早的 dynamic-web platform benchmark，仍可引用來支持「JNI／dynamic execution 的成本會受到 crossing pattern、data representation、workload 與 JVM implementation 影響」這類研究命題。

但不能用其歷史 JVM 的絕對 ns／µs 數字作為 Ckarta/OpenJDK 21.0.8 或 21.0.11 的測量值。

這不是因為「學術結果失效」，而是因為 external validity（外部效度）不足以把舊 implementation 的 absolute latency 直接移植到新的 JVM。

JNI benchmark：
Dawid Kurzyniec、Vaidy S. Sunderam，"Efficient cooperation between Java and native codes–JNI performance benchmark," PDPTA 2001。
https://www.researchgate.net/publication/228752983_Efficient_cooperation_between_Java_and_native_codes-JNI_performance_benchmark

Dynamic web：
Bhupesh Kothari、Mark Claypool，"Performance Analysis of Dynamic Web Page Generation Technologies," International Network Conference, 2000。
https://web.cs.wpi.edu/~claypool/papers/cgi-perf/

Varsha Apte、Tony Hansen、Paul Reeser，"Performance comparison of dynamic web platforms," Computer Communications 26(8), 888–898, 2003。
https://doi.org/10.1016/S0140-3664(02)00221-9

## 10. 對 Ckarta 設計的正式影響

到目前為止，「因 OpenJDK update 版本不同而無法定案」的部分可以縮減為 implementation-sensitive performance measurement，而不是 architecture rule。

可以正式定稿：

1. Ckarta 第一階段以 OpenJDK 21 API/JNI contract 為規範基線，並以固定 21u HotSpot source 追蹤 implementation。
2. JNI method invocation 的「會進入 HotSpot Java call machinery」可同時由 21.0.8／21.0.11 source 支撐，不再需要引用 current master 作為橋接證據。
3. `NewDirectByteBuffer` 作為 native-memory view 的 ABI 可以定稿；但不作「零成本」宣稱。
4. `GetPrimitiveArrayCritical` 不作一般 zero-copy strategy。
5. A/B/C thread topology 仍需實測；HotSpot source comparison 只能證明 lifecycle／call path，不會從 source reading 直接證明哪一種 topology throughput 或 tail latency 最佳。
6. 舊學術 benchmark 保留為 historical model evidence（歷史模型證據），絕對數值由 Ckarta 21u 實機 benchmark 取代。

## 11. 後續測量門檻

正式 thread benchmark 必須至少固定：

- OpenJDK distribution、exact build string、HotSpot mode。
- OS、kernel、CPU、compiler。
- warm-up 與 repetitions。
- worker／bridge topology。
- C canonical request descriptor 大小。
- DirectByteBuffer payload 大小。
- Java facade allocation。
- JNI invocation、queue wait、Java scheduling、GC／allocation 等分段時間。
- p50、p95、p99。
- cancellation／client disconnect／shutdown case。

這樣可以把「architecture evidence（架構證據）」與「performance evidence（效能證據）」完全分離，符合 Ckarta 的 WORKING_RULES.md 證據層級要求。 
