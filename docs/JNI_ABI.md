# Ckarta JNI ABI 基線

## 1. 定位

JNI ABI（JNI 應用程式二進位介面）是 C 資料平面與 Java Servlet 容器之間的最小邊界。

第一階段禁止建立「萬用 JNI API」。

## 2. Invocation

C main() 啟動 JVM。

Oracle JNI Invocation API：

https://docs.oracle.com/en/java/javase/17/docs/specs/jni/invocation.html

核心 API 包括：

JNI_CreateJavaVM
DestroyJavaVM
AttachCurrentThread
DetachCurrentThread
GetEnv

## 3. Descriptor

跨界資料必須以明確 descriptor（描述元）傳遞。

request descriptor 至少需要：

- identifier
- method
- target
- protocol version
- header view
- body state
- remote endpoint metadata
- native buffer reference
- lifetime token

response descriptor 至少需要：

- status
- header output
- body output state
- completion state
- error state

以上是 Ckarta 設計資料結構，不是現成 JNI API。

## 4. Buffer

Java 使用 native buffer 時：

ownership = C

Java = borrower

Java 不得 free。

若需要持久保存：

必須建立新的擁有權轉移機制。

## 5. Thread rules

JNIEnv pointer（JNI 環境指標）不可在執行緒間直接共享。

每個 native thread：

AttachCurrentThread
→ 使用自己的 JNIEnv
→ 完成工作
→ DetachCurrentThread

實際啟動與停止時序必須再以 JDK 17 測試確認。

## 6. Exception

所有 JNI 呼叫後都必須有 exception check（例外檢查）。

C 不直接操作 Java exception internals（例外內部實作）。

## 7. Crossing granularity

優先：

one request stage
→ one JNI transition

避免：

每個 header
每個 body chunk
每個 write operation

都跨越 JNI。

## 8. Async

JNI 不得假設：

CallStatic／CallVoidMethod 返回
=
request 完成

AsyncContext 可能使 request 在 Java method return 後繼續存在。

## 9. ABI stability

正式 ABI 進入穩定版前，必須有：

- version field
- struct size
- feature flags（功能旗標）
- reserved fields（保留欄位）
- explicit ownership flags（明確所有權旗標）

不得依賴 C struct 自然布局作為長期 ABI，除非另有明確 compatibility contract（相容性契約）。

## 10. Forbidden

禁止：

- expose raw socket fd to Servlet application（把原始 socket fd 暴露給 Servlet）
- expose C pool pointer
- Java free native memory
- C access private Java object internals
- hidden global native state

## 11. Native memory

所有 native allocation 必須可追蹤：

owner
lifetime
length
capacity
release rule

## 12. 後續實作門檻

以下文件完成並審查前，不建立正式 JNI public API：

docs/HTTP_FRAMING_POLICY.md
docs/CANCELLATION_MODEL.md
docs/CONCURRENCY_MODEL.md

