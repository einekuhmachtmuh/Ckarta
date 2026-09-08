# Ckarta 參考原始碼基線

## 1. 為什麼使用 Git submodule

Ckarta 不直接把 Nginx 與 Apache Tomcat 的完整原始碼複製成普通目錄。

採 Git submodule（Git 子模組）的原因：

1. 保留 upstream（上游）專案與 Ckarta 原始碼的明確界線。
2. 以 Gitlink（Git 連結）固定精確 commit，讓研究結果可重現。
3. 不把第三方專案的 Git 歷史、建置系統與授權檔案改寫成 Ckarta 的檔案。
4. 允許更新參考版本時進行明確的 diff（差異）與重新審查。

Submodule 不代表 Ckarta 在執行期依賴 Nginx 或 Tomcat。

## 2. 固定版本

### Nginx

版本：1.30.4 stable

Git tag：
release-1.30.4

commit：
017cf98dcce217946572a896f0992370475e189f

repository：
https://github.com/nginx/nginx

官方版本資訊：
https://nginx.org/2026.html

### Apache Tomcat

版本：11.0.25

Git tag：
11.0.25

commit：
cbe6e15ee81e2fc6232954292a80cca5d1e84009

repository：
https://github.com/apache/tomcat

官方版本資訊：
https://tomcat.apache.org/tomcat-11.0-doc/

## 3. 版本選擇

Nginx 以 stable 1.30.4 作為目前 Ckarta 的穩定參考基線，而不是 mainline 1.31.5。

Apache Tomcat 以 11.0.25 作為目前 11.x 的發布基線。

截至本文件建立時，Nginx 官方網站列出 1.30.4 為 stable，1.31.5 為 mainline；Tomcat 官方 11.x 下載頁列出 11.0.25。

版本更新不得只修改 submodule 指標；必須重新更新：

docs/HOT_PATH_REVIEW.md
docs/SECURITY_BASELINE.md
本文件

並記錄新舊 commit。

## 4. 研究用途

主要用途：

- function-level trace（逐函式追蹤）
- hot path（熱路徑）比較
- allocation path（配置路徑）比較
- blocking point（阻塞點）比較
- event／thread scheduling（事件／執行緒排程）比較
- request／response lifetime（請求／回應生命週期）比較
- security boundary（安全邊界）比較

禁止以「某行看起來相似」直接推導語意等價。

## 5. 修改政策

third_party/nginx 與 third_party/tomcat 預設唯讀。

不得：

- 在 submodule 內加入 Ckarta 私有程式碼；
- 修改 upstream 原始碼後忘記其 HEAD 已偏離固定 commit；
- 將 upstream 修改當成 Ckarta 新增功能。

若日後確實需要 patch upstream source（上游原始碼修補），必須：

1. 在 Ckarta 文件中記錄原因。
2. 保留可重現 patch。
3. 記錄 upstream commit。
4. 記錄 license／copyright 影響。
5. 設立測試證明修改。

## 6. 授權

Nginx 與 Apache Tomcat 具有各自的授權與著作權聲明。

Ckarta 不因使用 submodule 而取得修改或重新授權第三方程式碼的額外權利。

任何移植、複製或衍生程式碼都必須再次檢查適用授權與著作權聲明。

## 7. Git reproducibility（Git 可重現性）

研究或 benchmark（效能基準測試）必須記錄：

- Ckarta commit
- Nginx submodule commit
- Tomcat submodule commit
- compiler／JDK
- 作業系統
- 核心版本
- 建置參數

不得使用未固定的 upstream branch 作為正式 benchmark 基線。
