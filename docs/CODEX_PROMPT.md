請遵守 WORKING_RULES.md；若本提示詞與 WORKING_RULES.md、適用 normative authority 或 canonical document 衝突，以較高優先權者為準。
步驟來源優先級：WORK_STATE.md「下一個工程閘門」與目前有效 state > 前一次回答建議 > 尚未確認的產出規劃；不得以固定節號取代文件中的目前狀態判定。
證據來源：依 WORKING_RULES.md 的 authority 規則按問題領域選擇；在固定 Nginx/Tomcat 與 Linux kernel 的 implementation cross-check 中，可依 third_party/nginx → third_party/tomcat → Linux kernel 的順序比較，但 reference source 不得取代規格、官方 API/UAPI 或其他 canonical authority。
交叉比對僅限：演算法、資料結構、並行、記憶體、安全、與 Nginx/Tomcat 行為對齊；此範圍不排除 WORKING_RULES.md 要求的 normative-spec、API、ownership、lifecycle、source、CI 與其他必要驗證。
失敗時停止回報，不得繞過。
完成後執行 WORKING_RULES.md 所指定的驗證／品質閘門；涉及 exception/error handling boundary 時依第 21 節，不得將第 21 節本身誤稱為通用驗證閘門。
完成後依實際變更與 WORKING_RULES.md 要求，必要時更新 WORK_STATE.md、WORKING_TREE.md 與相關 canonical documents；不得為更新而更新。
輸出格式：目標、連續性檢查、變更摘要、證據、驗證、更新文件、下一步。
