# tick — 定期心跳（單次）

← [INDEX](../INDEX.md)｜[WORKFLOWS](../WORKFLOWS.md)｜由指令 [`/wf-tick`](../../.claude/commands/wf-tick.md) 循環喚醒

**做什麼**：跑**一次**定期心跳——就是**依序去執行各定期工作流**：[routines](routines.md)（固定例行）與 [schedule](schedule.md)（一次性請求）。刻意**極薄**：tick 自己不判時間、不存清單、不做事，只當**派發器**把各定期工作流叫起來，判斷與清單全在各工作流自己那邊。

> tick 是「定期喚醒引擎」的單次心跳，本身**只做派發**。真正的判斷與清單各歸被它叫起的工作流（[routines](routines.md) / [schedule](schedule.md)）。由 [`/wf-tick [週期]`](../../.claude/commands/wf-tick.md) 每隔週期喚醒一次。用不到定期喚醒就把 `tick` 連同 `/wf-tick` 一起刪。

## 單次心跳做什麼

1. **執行 [routines](routines.md) 工作流**：固定例行——判當地時間 → 對照時機分區 / 間隔登記表 → 到期的唯讀事務就做。
2. **執行 [schedule](schedule.md) 工作流**：一次性請求——判時間 → 到點的臨時定時請求就做、做完刪。
3. **回一句簡短心跳摘要**：兩個定期工作流這次各做了什麼 / 有沒有到期的。沒事就一句「無事」。

## 先不做

- **提醒 / 通知**：到期項目只在 session 裡當場做 / 當場說，不推任何外部通知。
