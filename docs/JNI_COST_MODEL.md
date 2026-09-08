# Ckarta JNI 跨語言成本模型

## 1. 研究基線

OpenJDK 21 Update：`jdk-21.0.8-ga`。

來源：
https://github.com/openjdk/jdk21u/tree/jdk-21.0.8-ga
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/

Nginx：1.30.4，commit `017cf98dcce217946572a896f0992370475e189f`。
Tomcat：11.0.25，commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。

## 2. 重要結論

JNI 不存在一個可泛化的固定成本數字。每次跨界成本必須拆成：

- JNI entry／exit（JNI 進入／離開）。
- argument marshalling（參數整理／搬運）。
- JNI handle 建立／解析。
- Java object allocation（Java 物件配置）。
- Java method invocation（Java 方法呼叫）。
- data copy（資料複製）。
- string／array materialization（字串／陣列物件化）。
- GC interaction（垃圾回收互動）。
- native memory lifetime synchronization（原生記憶體生命週期同步）。

任何沒有拆開這些因素的 benchmark 都不足以決定 Ckarta ABI。

## 3. OpenJDK 21：Call*MethodA/V

`src/hotspot/share/prims/jni.cpp` 的 `jni_Call*Method*` 使用 `JNI_ArgumentPusherVaArg` 或 `JNI_ArgumentPusherArray`，建立 `JavaCallArguments`，再進入 `jni_invoke_nonstatic`／`jni_invoke_static`，最後透過 `JavaCalls::call()` 執行 Java method。

因此 `Call*MethodA/V` 適合作為「一個有意義的 stage（階段）」之間的跨界，而不適合每標頭／每 body chunk（本文區塊）呼叫。

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp

## 4. OpenJDK 21：NewObjectA/V

`jni_NewObjectA`／`jni_NewObjectV` 的實際路徑包括：

`InstanceKlass::allocate_instance()`
→ `JNIHandles::make_local()`
→ `jni_invoke_nonstatic()`
→ `JavaCallArguments`
→ Java constructor。

所以若把一個 C struct 逐欄轉成 Java object，會同時支付：

物件配置 + constructor invocation + 欄位設定 + reference handling + 可能的 String／Array 配置。

因此 Ckarta 禁止 C struct → 多個 Java field 的逐欄 JNI 映射作為 request hot path（請求熱路徑）。

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp

## 5. Field API

`jni_GetFieldID()` 需要 class resolution／initialization 與 field lookup；instance field 的 `jfieldID` 以 field offset 為核心表示。

`jni_GetObjectField()` 會 resolve object、取得 offset、讀取欄位，且 object result 還要建立 local JNI handle。

因此：

C struct member load
不等於
JNI Get<Field>。

同理，C struct 的每個成員都呼叫 `Set<Field>` 也不是低成本的「memcpy 到 Java object」。

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp

## 6. Primitive arguments

`jint`、`jlong` 等 primitive（基元型別）可以避免為參數本身建立 Java reference object，但仍存在 JNI crossing 與 argument marshalling。

因此第一優先是一次跨界攜帶少量、穩定的 scalar metadata（純量中繼資料），而不是大量細粒度呼叫。

## 7. String API

JNI 規格提供多種 String access path（字串存取路徑）。`GetStringUTFChars` 等 API 可能涉及 copy（複製）；critical API 有額外限制。

因此 HTTP header name/value（HTTP 標頭名稱／值）不應在 C→Java 熱路徑中逐一生成 Java String。

推薦：

C 保留 canonical bytes（權威位元組）
→ Java facade 按需要 materialize String（建立字串物件）。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

## 8. Primitive arrays

`Get<PrimitiveType>ArrayElements` 在不同 JVM 情況可能是直接存取或 copy；Ckarta 不得假設一定不複製。

`Get/Set<PrimitiveType>ArrayRegion` 是明確的區段資料交換路徑。

`GetPrimitiveArrayCritical` 雖可能取得更直接的陣列資料，但 critical region 不可長時間持有、不可呼叫其他 JNI，也不可進行可能阻塞的 system call。

因此它不是一般 HTTP request path 的零拷貝策略。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html

## 9. DirectByteBuffer

`NewDirectByteBuffer` 會建立 Java `ByteBuffer` object，使其參照既有 native memory；JNI 規格不要求 payload copy。

OpenJDK 21 的 direct buffer 儲存 native address；`GetDirectBufferAddress` 可以取得同一資料區域位址。

因此它適合 Ckarta 的 bulk byte view（大量位元組視圖），但不會解決 lifetime／ownership。

來源：
https://docs.oracle.com/en/java/javase/21/docs/specs/jni/functions.html
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/java.base/share/classes/java/nio/Direct-X-Buffer.java.template

## 10. C struct → Java object 的核准策略

不採：

C struct
→ SetField × N
→ NewString × M
→ header object × K

採：

C canonical request
→ opaque request handle
→ 一個 Java request facade
→ 一次批次初始化
→ DirectByteBuffer data view

Facade 儲存少量 Java 可直接使用的狀態；大型位元組資料維持 C ownership。

對 header、query、cookie、body 等資料，只有在 Servlet API 真正需要 Java object 時才 materialize。

這是成本控制假設，不是已完成的 benchmark 結果。

## 11. JNI crossing 的優先級

最低優先級：

每 byte
每 header
每 object property

中間：

每個 request／response stage

高：

整個 request dispatch
整批 metadata
整批 byte view

## 12. API 選擇矩陣

| API | 主要成本 | Ckarta 用途 | 初步判定 |
|---|---|---|---|
| Call<Type>MethodA/V | crossing + marshalling + Java invocation | request dispatch | 有限 |
| NewObjectA/V | object allocation + constructor + JNI handle | facade 建立 | 有限 |
| Set/Get primitive field | crossing + field access | 少量初始化 | 避免大量 |
| Set/Get object field | crossing + reference handling | 非熱點 | 避免 |
| String access | 可能 copy／物件化 | lazy materialization | 避免高頻 |
| Primitive Array Region | 明確區段 copy | 批次資料 | 條件使用 |
| PrimitiveArrayCritical | strict critical region | 特殊短作業 | 非一般路徑 |
| NewDirectByteBuffer | Java buffer object + native view | 大量位元組資料 | 優先 |
| GetDirectBufferAddress | native address access | 已有 direct buffer | 優先 |

## 13. HotSpot implementation observation

OpenJDK 21 `jni.cpp` 顯示 JNI entry 會直接進入 HotSpot runtime path；JNI function table 另由 `jni.h` 定義。

這支持「不要把 JNI wrapper call count 當成唯一成本指標」的原則：實際成本取決於進入後執行的 runtime work。

來源：
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/hotspot/share/prims/jni.cpp
https://github.com/openjdk/jdk21u/blob/jdk-21.0.8-ga/src/java.base/share/native/include/jni.h

## 14. 學術依據

Dawid Kurzyniec、Vaidy Sunderam，"Efficient cooperation between Java and native codes – JNI performance benchmark"，Proceedings of the 2001 International Conference on Parallel and Distributed Processing Techniques and Applications，2001。

可閱讀來源：
https://www.researchgate.net/publication/228752983_Efficient_cooperation_between_Java_and_native_codes-JNI_performance_benchmark

Marian Bubak、Dawid Kurzyniec、Piotr Łuszczek、Vaidy S. Sunderam，"Creating Java to Native Code Interfaces with Janet"，Scientific Programming 9(1), 39–50, 2001。

DOI：10.1155/2001/582127
https://doi.org/10.1155/2001/582127

兩者均早於 OpenJDK 21，因此只作為 JNI 介面設計與成本研究的歷史學術依據，不把其微秒／奈秒數字直接套用 Ckarta。

## 15. 必須實測的 benchmark

後續必須使用 OpenJDK 21 + Ckarta 實際測量：

1. Java-to-Java baseline。
2. Java → native JNI。
3. C → Call*MethodA。
4. C → Java factory + opaque handle。
5. C → NewObjectA + scalar arguments。
6. C → 多次 Set/Get fields。
7. C → DirectByteBuffer。
8. array region copy。
9. primitive critical path。

同時記錄：

latency、throughput、allocation count、GC activity、bytes copied、JNI crossing count。

沒有這些結果，不得寫「Ckarta JNI 比某方案更快」。
