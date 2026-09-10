唯一執行提示詞：
先遵守 `WORKING_RULES.md`；本文件不得取代、重述或降低其規則，衝突時以較高優先權的規範、canonical 文件、Git 實際狀態與驗證結果為準。

每次工作先重讀最新 `WORKING_RULES.md`、`docs/WORK_STATE.md`、受影響 canonical documents、相關程式碼／測試、Git provenance、CI 與必要 reference sources；依 `WORK_STATE.md` 當前工程閘門執行，不得以對話記憶或未驗證規劃取代 repository 現況，也不得繞過 gate。

修改前重新核對目標檔案版本與相關依賴；遵守 preserve-then-integrate、ownership、lifecycle、concurrency、security、ABI、C11 與 formatting 規則。若有未知變更、衝突、驗證失敗或無法證明安全，停止並回報，不得猜測或繞過。

依 `WORKING_RULES.md` 第 21 節及其他適用 gate 驗證；CI 未完成、取消或不確定不得稱為通過，不得以局部測試冒充 compatibility／TCK。完成後檢查實際 diff、commit、CI 與文件一致性；只有實際需要時才更新 `WORK_STATE.md`、`WORKING_TREE.md` 或其他 canonical documents。

輸出只用：目標、連續性檢查、變更摘要、證據、驗證、更新文件、下一步。每節只報告必要資訊。