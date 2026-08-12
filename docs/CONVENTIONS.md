# 文件與註解整理規則

## 資訊放置

| 類型 | 放置位置 | 應回答的問題 |
|---|---|---|
| README | package／目錄旁 | 現在能做什麼、怎麼使用、入口在哪 |
| PLAN | `docs/<project>/future/` | 延後候選要做什麼、依賴、非目標、驗收條件 |
| NOTES | 專案的 `notes/` | 為何這樣決定、實測證據、容易重犯的坑 |
| 導讀／設計／探索／研究 | `docs/guides|design|explorations|research/` | 解釋、系統模型、發散問題、外部證據 |
| parked prototype | 專案的 `prototypes/` | 可執行但未升格的實驗，以及已知缺口 |
| 歷史材料 | `docs/archive/` | 已被取代但仍值得追溯的內容 |
| docstring／註解 | 程式旁 | 契約、限制、反直覺理由；不重述程式表面行為 |

`IDEAS.md` 只作尚未分類的收件匣；形成規格後移到對應 `PLAN.md`，落地後由 README
描述用法、NOTES 保存決策原因。

## 權威與衝突處理

最後修改時間不能決定真偽；衍生導讀可能比正式規格新，Git move/rebase 也會改變歷史外觀。
遇到衝突時依這個順序判斷：

1. 實際程式、測試與 package README 描述「現在能做什麼」。
2. 明確標為 canonical／定案的架構文件描述跨 package 邊界。
3. `ROADMAP.md` 描述已決定的先後；`future/` PLAN 保存延後方案，不代表正在實作。
4. research／exploration／guide 只提供證據、候選想法或另一種解釋，不可覆蓋前述真源。
5. 舊文有追溯價值就移入 `archive/` 並連回替代文件；否則刪除重複內容。

目前時間語彙的唯一來源是 [`freepy/agentloop/ROUNDS.md`](../freepy/agentloop/ROUNDS.md)：
一次完整 `agentloop.run()` 是 **Round**，每次 `ask() → message` 是 **Step**；tool call
發生在兩個 Step 之間，不另造一個時間層級。

## 程式註解

- 公開介面用 docstring／header comment 說明輸入、輸出、錯誤與所有權。
- 行內註解優先解釋「為什麼不能用看似更簡單的寫法」。
- 不在註解保存 roadmap、工作日誌或會快速過期的模型／環境清單。
- 行為改變時，同一次修改更新相鄰註解與對應 README。

## 檔案大小

- Markdown 單檔保持在 8 KiB 以下；超過時依概念或閱讀順序拆檔，由短 README 提供導覽。
- HTML 優先拆成數個小頁並共用本地 CSS；單頁保持在 8 KiB 以下，不用內嵌重複樣式撐大檔案。
- 拆檔不能犧牲唯一真源：導讀頁連回正式規格，不複製一份會獨立漂移的完整規格。
