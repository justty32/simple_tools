# P1a-1 使用筆記

> 工作稿心得，不可提升為正式規格。

## 支持的假設

- `child_planned` 再 `materialize` 再 `child_linked` 讓「child 何時可跑」比掃目錄清楚；crash 在任一格後可以重新走同一 deterministic child ID。
- 已有 semantic Receipt 時先驗 hash，再拒絕 dispatch，能直接量到 recovery 不增加 dispatch count。
- `dispatch_intent` 而沒有可信 Receipt 時立刻停在 `repair_required`，比猜 PID 或重送誠實。

## 不舒服與尚未回答

- 每 Task 各自 JSONL 很容易看懂，但 parent/child 同步關係仍不是跨檔交易；本實驗只靠固定順序及 recovery 補齊，不能外推 multi-writer。
- 只有 parent 有 Call bytes 很省重複，但 child 讀 parent 路徑時把 parent 的保存期變成 child 正確性的前提；刪除/搬移/portable image 尚未處理。
- fake leaf 沒有 P0 staging evidence。P1a-2 必須驗「staging 完整但 semantic Receipt 未 commit」只能補 commit，不能重跑 process。
- `repair_required` 這裡只是原型狀態；它能否修復、能否轉 terminal 或如何交給使用者尚未決定。

## 是否進 P1a-2

建議在 fault matrix 穩定通過、並由設計端接受這個 parent-owned Call 引用方向後再進。下一片只接一個 deterministic P0 success/unknown case；不要同時帶入 Agent、Git 或 common result ABI。
