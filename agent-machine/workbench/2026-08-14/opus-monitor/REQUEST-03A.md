# Opus 設計補充請求 03A

**Status: RECEIVED**

已收到 [`OPUS-ADDENDUM-03.md`](OPUS-ADDENDUM-03.md)。以下保留本輪原始請求。

使用者在 Round 03 完成後補充了同一 agent 忙碌時的正式方向。這次不要重寫前兩份文件；請修正受影響的部分，輸出一份：

- `OPUS-ADDENDUM-03.md`

全文繁體中文、先寫結論、控制在 8 KiB 內，讓 C++ 工程師不用查術語也能讀懂。

## 只讀來源

請依序只讀以下四份，不要跟隨其中的連結：

1. `agent-machine/workbench/2026-08-14/idea-ledger/USER-DECISIONS.md`
2. `agent-machine/workbench/2026-08-14/idea-ledger/OPEN-QUESTIONS.md`
3. `agent-machine/workbench/2026-08-14/opus-monitor/OPUS-DESIGN-03.md`
4. `agent-machine/workbench/2026-08-14/opus-monitor/OPUS-REVIEW-03.md`

不得掃描 repo，不得讀其他候選頁、prototype 或舊審查。不要修改來源、前兩份回覆、Request 或 README；只在本資料夾新寫 `OPUS-ADDENDUM-03.md`。

## 使用者已定，這次不可重議

1. agent root 忙碌時，普通新呼叫預設直接回覆「正在忙」，而且不建立 Task。
2. 使用者可用明確操作，把新 prompt 追加到目前 Task；這不是普通新呼叫，也不建立另一個 Task。
3. 使用者可用另一個明確操作建立新的排隊 Task，但必須附上「什麼狀況才可開始」的條件。
4. 判斷排隊 Task 能否開始時，不能只看前一個 Task；同一 root 有兩個 Task 同時執行可能弄亂資料夾。
5. 若同一 root 同時有兩個 active Tasks，agent 的資料夾內必須看得見這件事；資訊由 AOS 管理，目的是讓正在該地盤工作的 agent 知情。

你在 Round 03 推薦「普通外來 Call 預設排隊」，已與第 1 點衝突。請明確撤回並以新方向取代，不要再比較「預設排隊或預設忙碌」。

其他細節仍是候選：Task 完整生命週期、tree lease（整棵工作樹共用寫入權）、child 是否繼承、唯讀是否並行、版本數量、正式檔名、命令與狀態名稱，都不是使用者定案。

## 必答一：重新整理 tree lease

你可以保留、縮小或改掉 tree lease，但要解釋它在新政策下還負責什麼：

- 普通呼叫的「拒絕」發生在建立 Task 前；明確排隊則已建立 Task，但尚未取得執行資格。兩者不能混成同一種 queue。
- 明確 append 如何在安全的 Step／Round 邊界交給目前 Task，而不是變成第二棵 tree。
- parent 呼叫 child 時，如何避免 parent 等 child、child 又等同一 root 寫入權的死鎖。
- 若允許進階並行，tree lease 是唯一可寫者、只保護發布點，還是應拆成別的概念。

先說推薦，再給一個會推翻它的具體反例。

## 必答二：比較三套同 root 並行方案

主線仍以避免同 root 多個可寫 Task 為預設；以下都是使用者明確選擇後才啟用的進階方案：

1. **完全序列化**：同一時間只有一棵可寫 Task tree；其餘只等待或讀取已發布資料。
2. **共用 root 的受限並行**：多個 Task 可讀取或計算，但寫入只能在受控發布點發生；請說明是單一發布者、預先分開寫入區，或更小的規則。
3. **隔離工作區**：每個 Task 在自己的 worktree／staging（私人工作區／暫存區）修改，完成時才檢查衝突並發布。

每案請用短表格比較：可安全並行的讀、計算、root 寫入與外部作用；優點、代價、失敗反例；適合哪個階段。外部寄信或 API 不會因丟棄私人工作區而回滾，必須明說。最後選一案作第一個 Python 小原型。

## 必答三：中央真相到 agent 知情的邊界

請設計並檢查這條候選鏈：

```text
中央 AOS 的排程真相
  -> agent root 內可丟棄、可重建的本機投影
  -> 正在工作的 agent 知道 active Tasks 已改變
```

中央資料才有權開始、停止或授予寫入資格；資料夾投影只供 agent 觀察，不可反過來成為第二份真相。這是本輪要驗的候選，不是使用者已裁決的 `.aos` 位置或 Git 邊界。

請回答：

- 啟動一個 Task 前，投影最晚何時必須可見？執行中 active set 改變時，agent 最晚何時必須知道？
- 只寫檔不等於已通知。比較「每個 Step／Round 邊界重讀」、「AOS 主動送事件」、「agent 自行查詢」三種做法，只選原型方向，不定 signal、socket 或命令。
- AOS 在中央狀態已改、投影尚未更新時 crash，或投影缺失、半寫、過期時，如何辨認舊資料、重建並避免 agent 把提示誤當執行授權？
- 投影被重建時如何避免讓執行中的 agent 誤以為 active Tasks 短暫歸零？

請用至少兩個 crash／stale（過期）時序反例檢查設計。

## 必答四：排隊 Task 開始前的兩道 gate

明確建立的排隊 Task，每次嘗試開始都必須同時通過：

1. **條件 gate**：使用者指定的開始條件已成立。
2. **root 安全 gate**：root 當下確實有容量或隔離空間，且中央狀態與資料夾投影沒有讓這次啟動看見過期資訊。

請定義兩道檢查的先後與同一判定點、AOS crash 後為何必須重驗，以及任一關不成立時應繼續等待還是停住請人處理。root capacity 不是先固定一個數字；它要表達「依所選並行方案，現在能否安全授權」。不要設計條件語言、正式狀態列舉或 CLI。

## 輸出結構

1. 結論與對 Round 03 的明確修正。
2. 哪些是使用者決定、哪些仍是你的候選。
3. 修訂後的 tree lease。
4. 三套並行方案比較與推薦。
5. 中央真相 → 本機投影 → agent 通知的資料流與故障恢復。
6. 兩道 gate 的判定流程。
7. 不超過 4 步的最小 Python 原型順序；每步寫「完成後能決定什麼」。

不得自行定正式檔名、資料夾位置、JSON 格式、CLI、signal 或 socket。示意名稱必須標明不是規格。不展開跨機器、symlink、安全 sandbox、MCP、coding agent、SQLite 或完整 Runtime 形狀。
