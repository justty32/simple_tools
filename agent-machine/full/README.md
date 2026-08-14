# AOS 完整設計導覽

`full/` 是 AOS 的完整設計稿：它描述系統由 Function 到 Agent 的整體目標、邊界與安全規則。**完整設計不等於全部已實作**；目前只有少數 process 與保存切片有原型證據。每個結論都要保留狀態，不得把推薦、原型格式或待選方案寫成既成事實。

想先知道「現在真的跑得動什麼」，仍要看程式、測試與相鄰 README；本套回答的是「AOS 要成為什麼，以及各部分如何接起來」。

## 權威順序

遇到衝突時先判斷問題類型：

1. **產品方向與用詞**：使用者較新的明確決定最高；[`00-STATUS-AND-SOURCES.md`](00-STATUS-AND-SOURCES.md) 收斂共同邊界。
2. **目前已實作行為**：實際程式、測試與其 package README 為準；原型只證明測過的窄範圍。
3. **完整設計內部**：本目錄各主題頁依 00 的狀態標記解讀；主題頁比本導覽的摘要更精確。
4. **尚未裁決的選擇**：以 [`options/README.md`](options/README.md) 為準；它們不是承諾。
5. **研究與過程**：[`workbench/`](../workbench/2026-08-14/README.md) 提供實驗證據與決策脈絡，但不是正式 ABI（跨語言固定介面）。
6. **歷史**：[`archived/`](../archived/README.md) 只供追溯，沒有權威。

## 狀態標記

- **使用者已定**：使用者親自確定的方向；實作選擇不可違反。
- **目前推薦**：完整稿目前主張的安全做法；仍可被原型或使用者決定替換。
- **原型暫選**：為使某個實驗可重現而固定，只能在該證據範圍內使用。
- **尚未決定**：保留多案，不得由文件作者偷偷補成唯一答案。
- **明確延後**：方向已知但不進近期主線，也不能假裝已完成。

同一頁可同時出現多種標記；沒有標記的細節不得自動視為「使用者已定」。完整定義見 00。

## 由小到大的完整閱讀路線

建議依序讀 00 到 13；前一頁建立後一頁會使用的概念。

| 頁面 | 唯一責任 |
|---|---|
| [00 狀態與來源](00-STATUS-AND-SOURCES.md) | 定共同詞義、權威、狀態、來源取捨與全套不變規則。 |
| [01-FUNCTION-AND-FILESYSTEM-MEMORY.md](01-FUNCTION-AND-FILESYSTEM-MEMORY.md) | 解釋 AOS 的核心比喻、Function 定義與 memory 模式邊界。 |
| [02-LEAF-PROCESS-CALL.md](02-LEAF-PROCESS-CALL.md) | 定 Linux process 作為最底層 Function 的輸入、證據與失敗邊界。 |
| [03-CALL-TASK-RETURN.md](03-CALL-TASK-RETURN.md) | 分開請求、執行實例、呼叫者結果與持久證據，不把 Task 當 PID。 |
| [04-COMPOSITE-FUNCTION-AND-TASK-TREE.md](04-COMPOSITE-FUNCTION-AND-TASK-TREE.md) | 說明 Function 呼叫 Function、父子關係與結果組合。 |
| [05-DURABLE-STATE.md](05-DURABLE-STATE.md) | 定權威狀態、落盤屏障、結果不明與 machine-local 記錄原則。 |
| [06-AOS-LIFECYCLE-AND-RECOVERY.md](06-AOS-LIFECYCLE-AND-RECOVERY.md) | 描述 AOS 自己的 start、recover、ready、drain、stop 與重開安全。 |
| [07-CHECKPOINT-GIT-AND-PORTABILITY.md](07-CHECKPOINT-GIT-AND-PORTABILITY.md) | 分開可攜 checkpoint 和某台機器仍在執行的狀態。 |
| [08-SCHEDULING-AND-EXECUTION.md](08-SCHEDULING-AND-EXECUTION.md) | 說明接受、條件、容量、寫入權與 dispatch 的先後。 |
| [09-AGENT-ROOT-AND-OWNERSHIP.md](09-AGENT-ROOT-AND-OWNERSHIP.md) | 定 Agent 是 AOS 上層擴充、絕對 path identity 與地盤責任。 |
| [10-STEP-TOOL-ROUND.md](10-STEP-TOOL-ROUND.md) | 定三種 Agent 角色如何映射到 Function／Task，避免混名。 |
| [11-SHELL-CLI-AND-PATH.md](11-SHELL-CLI-AND-PATH.md) | 描述人類操作層如何降成明確 Function 呼叫，不先凍結命令語法。 |
| [12-IMPLEMENTATION-AND-EVIDENCE.md](12-IMPLEMENTATION-AND-EVIDENCE.md) | 區分 Python 試作、C++／Janet 邊界、測試通過與產品保證。 |
| [13-LIMITS-AND-LATER.md](13-LIMITS-AND-LATER.md) | 集中列出非目標、風險與延後項目，避免其他頁暗示已處理。 |

讀完主線再進 [`options/README.md`](options/README.md)。options 依序比較：Function directory 入口、Task 生命週期、filesystem memory 模式、Return／unknown／repair、`.aos` 與 store、scheduler 形狀、Agent busy 與 Step 邊界、CLI 表面、C++／Janet bridge。選項頁負責保留取捨，不得反向改寫主線的狀態。

00 到 13 與 `options/` 九份選項頁均已建立。

## 研究與歷史入口

- [2026-08-14 workbench](../workbench/2026-08-14/README.md)：使用者決定、原型、測試邊界與 Opus 取捨。
- [archive 導覽](../archived/README.md)：被取代的 AOS／AgentOS 文件與原始 snapshot。
