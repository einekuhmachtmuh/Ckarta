# Jakarta Servlet 6.1 TCK 整合計畫

## 1. 目的

TCK（Technology Compatibility Kit，技術相容性套件）是最終 Servlet 6.1 相容性判定的重要驗證工具。

功能測試通過不等於規格相容。

## 2. 驗證層級

第一層：

C parser／network tests（網路測試）

第二層：

C/Java JNI integration tests（C/Java JNI 整合測試）

第三層：

Servlet API tests

第四層：

Jakarta Servlet 6.1 TCK

## 3. 必須分離

Ckarta 自有測試不得取代 TCK。

TCK 結果必須獨立保存。

## 4. TCK environment

正式整合時記錄：

- TCK version
- Jakarta Servlet version
- JDK version
- operating system
- Ckarta commit
- configuration
- JVM options

## 5. Fail policy

任何 TCK failure（失敗）都不能標示 Servlet 6.1 compatible（相容）。

## 6. 目前狀態

目前已固定 `jakarta.servlet:jakarta.servlet-api:6.1.0` 作為 application-facing API 的 compile/test dependency，並有 `CkartaServletAsyncContext` API binding prototype 與獨立 smoke test。尚未執行 Jakarta Servlet 6.1.0 TCK；因此目前仍不得標示 Servlet 6.1 相容。

本文件是整合計畫，不代表目前已通過 TCK。

