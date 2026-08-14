# Opus 獨立設計請求 03

**Status: RECEIVED**

已收到 [`OPUS-DESIGN-03.md`](OPUS-DESIGN-03.md) 與
[`OPUS-REVIEW-03.md`](OPUS-REVIEW-03.md)。以下保留本輪原始請求。

這一輪請你當 AOS 的獨立架構設計者，不只檢查我們的答案。你可以反對候選頁的建議，並提出更簡單、較一致的整套模型。

一次完成兩份輸出：

- `OPUS-DESIGN-03.md`：你自己的完整、分階段 AOS 設計支線。
- `OPUS-REVIEW-03.md`：對我們候選方案的取捨，以及你建議修改之處。

兩份都完成才算本輪完成。每份請控制在 8 KiB 內；不要用大量重複文字填滿篇幅。

## 只讀來源

請依序只讀下列六份檔案，不要跟隨其中的連結：

1. `agent-machine/workbench/2026-08-14/idea-ledger/USER-DECISIONS.md`
2. `agent-machine/workbench/2026-08-14/idea-ledger/OPEN-QUESTIONS.md`
3. `agent-machine/workbench/2026-08-14/idea-ledger/TASK-LIFECYCLE-CANDIDATES.md`
4. `agent-machine/workbench/2026-08-14/idea-ledger/FILESYSTEM-MEMORY-CANDIDATES.md`
5. `agent-machine/workbench/2026-08-14/idea-ledger/MODULE-EVOLUTION-CANDIDATES.md`
6. `agent-machine/workbench/2026-08-14/idea-ledger/CPP-JANET-PHASE-CANDIDATES.md`

不得掃描 repo，不得讀舊 Opus 回覆、`round-2-decision` 或任何 prototype。尤其不要讀正在修改的 `p1a2-process-python`。不要修改來源；只可在本資料夾新寫上述兩份輸出。

## 不能偷偷改掉的起點

`USER-DECISIONS.md` 是使用者已親自決定或表達偏好的方向。其他五份都是內部候選；其中寫「建議」也不代表使用者已同意。

特別注意：使用者只明確說「正在執行中的 Function 實例叫 Task」。Task 是否從落盤接受、排隊一路延續到完成紀錄，仍是候選。Call、Receipt、event、Attempt 與各種版本號也都還不是正式 AOS 規格。

本文的 agent root 是 `/home/mine/agents/bot-a` 這類 agent 地盤，不是 Task tree 最上層節點。Filesystem memory 是「檔案系統作為可持久記憶」的 AOS 抽象，不是把硬碟假裝成 RAM。

## 四個必答設計題

請把四題組成一套互相配合的模型，不要各自孤立作答。

1. **Task 生命週期**：Task 從何時有身分？排隊、執行、暫停、AOS 重啟、完成後是否沿用同一 ID？一次實際 process 啟動要不要只是內部細節？
2. **同一 agent 忙碌時怎麼辦**：新的可寫工作預設排隊、直接回覆忙碌，還是另有更簡單做法？唯讀互動、parent／child 工作與取消如何不破壞「一個地盤一個主事者」？
3. **Filesystem memory 怎麼分區**：Function 定義、agent 可變記憶、不可修改的執行證據、本機排程狀態、可攜 `.aos` 存檔，各自該採即時讀寫、核對版本、暫存後發布或固定快照中的哪一種？AOS 自己重啟後如何辨認可安全續接的狀態？
4. **由小到大的成長路線**：單一可執行檔如何長成 directory Function、會呼叫子工作的組合 Function，最後才成為 child agent？固定 `call`、入口設定檔與 `.aos` 各放在哪一階段，才能讓 `aos exec ./ask` 保持直覺？

## `OPUS-DESIGN-03.md` 要包含

- 先用一段話寫結論，再列「使用者已定的起點」與「你新增的設計」。
- 一套你推薦的整體模型，說明上述四題如何彼此影響。
- 除推薦案外，至少兩套真的可採用的整體替代方案；用短表格比較優點、代價與適用時機。
- 每個關鍵選擇至少一個具體反例：用路徑、命令或小故事說明錯誤設計會怎麼壞。
- 由最小到較大的原型順序。先用 Python 驗語意，再指出哪些界線必須由 C++ 重驗、Janet 最適合負責什麼；每一步都寫清楚「跑完後可決定哪一題」。
- 明列你認為我們應改掉、延後或重新命名的假設。可以自由反對來源中的推薦案。

這份要能單獨閱讀；不要寫成逐段批註，也不要先定 JSON 格式或完整目錄 ABI。

## `OPUS-REVIEW-03.md` 要包含

- 先下結論，接著用「保留／修改／捨棄／延後」整理候選方案。
- 清楚標出哪些只是候選，不可誤寫成使用者決定。
- 指出候選頁彼此衝突、重複或不必要複雜的地方。
- 列出會推翻推薦案的反例，以及最小的修正方式。
- 最後給主 agent 一張不超過 8 項的優先處理清單；分清楚「現在做原型」和「先不做」。

這份要對照我們的方案，不要重抄 `OPUS-DESIGN-03.md`。

## 寫法與範圍

- 全文使用繁體中文，先結論，寫給 C++ 工程師看。
- 保留 AOS、Function、Call、Task、Agent、Step、Round、Tool 等必要名稱；其他英文或抽象詞第一次出現時立刻用白話解釋。
- 多用具體命令與目錄例子，少造新名詞；不要塞正式 schema。
- 只處理單機主線與日後 Python → C++／Janet 的驗證順序。
- 不展開跨機器、網路同步、symlink、外力改名、coding agent、MCP 或完整 server API。
- 本輪不是 process／crash implementation gate；那一輪已另留 `REQUEST-04.md`。

完成後不要改本 Request 的狀態，也不要修改 README。
