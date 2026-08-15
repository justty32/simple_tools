# AOS 選項精華（一）：Function、Task、memory 與保存

這是 [`full/options/01–05`](../full/options/01-FUNCTION-ENTRY.md) 的決策地圖，不是定案。文中的「目前推薦」是下一個最小原型的起點；只有使用者明確裁決或反例測試排除其他方案後，才可提升。

## 短詞彙

- **Definition**：可被呼叫的工作定義。
- **Call**：一次請求；不等於執行實例。
- **Task**：受 AOS 管理的一次執行實例；不是 PID。
- **Attempt**：Task 內一次具體啟動；不是自動重試許可。
- **Return**：caller 看見的語意結果。
- **Receipt／持久證據**：哪個 Task 根據哪些可信依據得到哪個 Return。
- **Unknown**：外部作用可能已發生，但缺可信完整結果。

## 01｜Function 資料夾入口

**決定什麼：**資料夾如何明確指出可呼叫入口，以及哪些 helper／prompt／設定算 Definition。使用者已定「檔案或資料夾可承載 Function」，入口尚未決定。

**狀態：**資料夾可承載 Function 是**使用者已定**；精確入口是**尚未決定**；固定 `call` 是**目前推薦**的小原型。

**目前推薦：**先用固定 `call` 入口做 file → directory 小原型，再比較 manifest；不猜 `main.py` 或 `run`。薄 launcher 只作相容遷移，獨立 `.aos` 描述檔延後。

**代價：**固定入口最簡單，卻說不清多檔 Definition 範圍；manifest 可驗證依賴，但增加解析、版本與雙真源風險。

**推翻證據：**放入假的 `main.py`、改 helper、缺入口、壞 manifest；要求所有錯誤在 effect 前拒絕。若固定入口不足以辨認真正依賴，再升級 manifest。

**使用者裁決：**目前不需要；要把入口名稱或 manifest 提升成公開契約時才需要。

原文：[`options/01`](../full/options/01-FUNCTION-ENTRY.md)。

## 02｜Task 生命週期與身分

**決定什麼：**Task 在 accept 還是 dispatch 時取得身分，是否跨 queued／paused／completed 保持同一 ID，以及 process 啟動是否另記 Attempt。

**狀態：**「執行中的 Function 實例叫 Task」是**使用者已定**；從 accept 到完成是否都叫 Task 是**尚未決定**；固定 Task ID 加 Attempt 是 **P1 原型暫選**。

**目前推薦：**安全落盤接受後配置固定 Task ID，Runtime 內部再分 Attempt；日常畫面先只呈現 Task。這是候選，不是使用者對 Task 起訖的正式定義。

**代價：**單一 ID 最利於 CLI、parent relation 與重開，但把 Task 一詞延伸到尚未執行與完成記錄；Call ID → Task ID 雙身分較字面精確，卻多一段 crash-sensitive 升格關係。

**推翻證據：**在 accept 後、dispatch 前殺掉 manager；比較單 ID／雙 ID 的查詢、parent 等待與重連。

**使用者裁決：**公開 Task 介面前，需選「Task 只指 running」或「接受後一路沿用」；測試只能提供代價，不能代選詞義。

原文：[`options/02`](../full/options/02-TASK-LIFECYCLE.md)。

## 03｜Filesystem memory 模式

**決定什麼：**Definition、可變 memory、持久證據、本機狀態各用 live、checked-live 還是 snapshot／staging。

**狀態：**filesystem 是 AOS memory 的方向是**使用者已定**；各資料類型的精確模式**尚未決定**；混合模式是**目前推薦**。

**目前推薦：**依資料類型混合：root 保持 shell 友善的 live 外觀；Definition 在作用前檢查；memory 由 staging 比對 base 再發布；持久證據不可原地改寫；claim／PID 留在本機。

**代價：**全 live 最簡單但不可重現；checked-live 仍有 check-to-use 空窗；snapshot 較穩定，卻帶來複製、衝突、清理與發布交易。Snapshot 也不能倒回寄信或 HTTP。

**推翻證據：**在 hash 後、spawn 前替換入口；讓兩個工作由同一 base 發布；讓一個 Task 產生新版 Definition。若 checked-live 的空窗不可接受，再做最小 snapshot。

**使用者裁決：**目前不需要；要承諾可重現等級或多人衝突 UX 時才需要。

原文：[`options/03`](../full/options/03-FILESYSTEM-MEMORY-MODES.md)。

## 04｜Return、unknown 與 repair

**決定什麼：**Common Return ABI、結果不明的生命週期，以及如何用新證據修復或明確重做。

**狀態：**Common Return ABI 與正式 repair 介面**尚未決定**；「unknown 不自動重送」是**目前推薦**，也是 P1 原型的安全線。

**目前推薦：**先安全保存並驗證持久證據，再由它支持 known Return；Return 與 Receipt 仍是不同角色。Nonzero、signal、launch error 可是已知 Return；有 intent 卻缺完整結果則停在 unknown，不自動 retry。能唯一推導的投影可補，仍有兩種可能就等待 repair。

**代價：**這比「process 結束就回傳」多出保存與驗證成本，也會把不確定性暴露給使用者；但把 unknown 當普通失敗可能重複寄信、扣款或改檔。

**推翻證據：**在外部 fake effect 完成、結果提交前殺掉 manager；重開後 effect 不重送。只有某類 Function 經證明無副作用或有可查驗防重 key，才為該類放寬。

**使用者裁決：**目前不需要；公開 unknown／repair 操作或允許特定類別自動重試前才需要。

原文：[`options/04`](../full/options/04-RETURN-UNKNOWN-AND-REPAIR.md)。

## 05｜`.aos` 與中央 store

**決定什麼：**哪些 live 執行權威留中央，哪些穩定語意進 Agent repo／Git，以及 checkpoint 如何搬到新機。

**使用者已定：**偏好 `.aos`、Agent root 可進 Git；可攜狀態與本機 active／claim 不可混在一起。

**狀態：**上述分界是**使用者已定**；中央 store 加 repo checkpoint 是**目前推薦**；版面、資料庫與 checkpoint 自足程度都**尚未決定**。

**目前推薦：**採「中央執行 store + repo 內 `.aos` checkpoint」的責任分工，但不固定檔名或資料庫。Root 內 active 顯示只是被 Git 忽略的本機投影。Clone 後先 detached／paused，零自動 dispatch。

**代價：**中央通常比 checkpoint 新，兩邊發布順序要明定；全部放 root 容易誤 commit PID／claim，全部放中央又讓 clone 缺少可攜記憶。內容物件庫利於去重，卻會提前引入 GC 與缺件問題。

**推翻證據：**Exporter 只接受沒有活 worker、unknown effect 或未發布 staging，且世代與續接點明確的 `paused-safe` 穩態；並須拒絕 PID、FD、lock、claim。Clone／attach 後啟動數必須為 0。

**使用者裁決：**正式 portable 格式前，需取捨 checkpoint 要多自足、是否人類可讀；通過前不定 `.aos` layout。

原文：[`options/05`](../full/options/05-DOT-AOS-AND-STORE.md)。

下一頁：[排程器、busy、CLI 與語言邊界](2026-08-14-options-essence-02.md)。共同狀態邊界見 [`full/00`](../full/00-STATUS-AND-SOURCES.md)。
