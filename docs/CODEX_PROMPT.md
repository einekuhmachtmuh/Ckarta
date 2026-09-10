唯一執行提示詞：
請先遵守 `main/WORKING_RULES.md`；本文件不得取代、重述或降低其中任何規則。若本文件與 `WORKING_RULES.md`、適用 normative authority、canonical document、Git 實際狀態或驗證結果衝突，以較高優先權者為準。
每次工作先重新讀取最新 `main` 的 `WORKING_RULES.md`、`docs/WORK_STATE.md`、受影響 canonical documents、相關程式碼／測試、Git provenance、CI 與固定 reference sources；不得以對話記憶或本文件取代 repository 現況。步驟依 `WORK_STATE.md` 當前有效的下一個工程閘門與 state 執行；前次回答與尚未確認的規劃僅作低優先參考，不得繞過 gate。
證據依 `WORKING_RULES.md` 的 authority 規則按問題領域選擇。`third_party/nginx`、`third_party/tomcat` 與 Linux kernel 只在適用範圍內交叉比對，不得取代規格、官方 API/UAPI、canonical document 或實測結果。
涉及修改時，先核對目標檔案最新內容／版本與相關依賴，遵守 preserve-then-integrate、ownership、lifecycle、concurrency、security、ABI、C11 與 formatting 規則；完成後檢查實際 diff、commit、CI 與受影響文件一致性。發現衝突、未知變更、驗證失敗或無法證明安全時立即停止並回報，不得繞過或猜測。
依變更風險執行 `WORKING_RULES.md` 第 21 節所定義的 testing/verification gates 與其他適用驗證；CI 未完成、取消或不確定時不得稱為通過；不得以局部 smoke test 冒充 compatibility/TCK certification。
只有在實際變更要求時才更新 `docs/WORK_STATE.md`、`docs/WORKING_TREE.md` 與相關 canonical documents；不得為更新而更新。
輸出僅用：目標、連續性檢查、變更摘要、證據、驗證、更新文件、下一步。每節只報告本次真正需要知道的內容，避免重複與冗餘。
