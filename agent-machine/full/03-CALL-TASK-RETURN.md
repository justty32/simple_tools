# Call、Task 與 Return

本頁只分開「想做什麼、哪次工作正在做、呼叫者拿到什麼、磁碟上能證明什麼」。Leaf 證據見[02](02-LEAF-PROCESS-CALL.md)，多層組合見[04](04-COMPOSITE-FUNCTION-AND-TASK-TREE.md)。

## 七個不同角色

| 角色 | 白話意思 | 狀態 |
|---|---|---|
| Function Definition | 可被呼叫的工作定義，由明確檔案或資料夾承載。 | **使用者已定** |
| Call | 呼叫請求：哪個 Definition、哪些參數與輸入。不要把執行狀態塞回請求。 | **目前推薦**；正式名稱與 immutable 邊界未定 |
| Task | Function 的一次受 AOS 管理的執行實例；不是 PID。 | **使用者已定**正在執行時的稱呼；完整起訖未定 |
| Attempt | Task 內某一次實際啟動；PID、worker 與啟動錯誤屬於這一層。 | **目前推薦** |
| Function Return | Function 回給 caller 的語意結果。 | **目前推薦**；common ABI 未定 |
| 持久證據／Receipt | 說明某 Task 根據哪些可信依據得到哪個 Return。 | 角色切分為**目前推薦**；`Receipt` 名稱與格式未定 |
| Tool Result | Agent 層交回模型、與 Tool Call 配對的結果。 | **目前推薦**；不是 Return 或 Receipt 的別名 |

**目前推薦：**同一份資料可以被下一層引用，卻不因此變成同一物件。例如 process 的 stdout 是 leaf evidence；Function 可把它解讀成 Return；Task 再留下持久證據；Agent Tool adapter 最後才包成 Tool Result。

## Task 不是 PID

### 使用者已定

- 正在執行中的 Function 實例叫 Task。
- Task 不能用 PID 定義：Function 可為 composite，一個 Task 也可能先後啟動不同 process。

### 目前推薦

- PID 只是某個 attempt 的本機觀察值，會重用、不能跨重開或跨機當身分。
- 排隊請求、執行中的 Task、已完成記錄應能被清楚區分；pause、等待 child 或重開不應靠換 PID 冒充新工作。
- Call 驗證或 admission 被拒絕時，不應留下看似已接受的 Task。Agent busy 的普通拒絕更是使用者已定為「零 Task」。

### 原型暫選

- P1 在 durable accept 時配置固定 Task ID，讓同一 ID 暫時貫穿 queued、running、paused 與 completed；每次 process 啟動只是內部 attempt。
- P1 的 immutable Call、Task-local `call.json`、`task.json`、hash ref、event 與 ID 形狀，只是保存實驗。
- 相同 Call bytes 可建立不同 Task IDs；這支持「請求內容不等於執行實例」，但不決定產品 ID 或 public accept 的重送語意。

### 尚未決定

- Task 正式從 durable accept、dispatch 還是真正開始執行算起；完成後是否仍沿用 Task 一詞。
- Task ID 的配置、公開重送 key、保存期限、查詢、壓縮與清理。
- Pause／resume 是否新增 attempt，以及取消、repair 或明確重做應沿用舊 Task 還是建立新 Task。

## Return、錯誤與 unknown

### 目前推薦

- Function Return 是 caller-facing 結果；Task state 是 AOS 對執行生命週期的判斷。兩者不能互相取代。
- Leaf 的非零 exit、signal 或 launch error 可以是完整已知 Return。它可能代表業務失敗，但不是「AOS 不知道發生什麼」。
- **Unknown** 表示缺少可信完整結果，是證據狀態，不是假造的 error Return，也不是自動 retry 許可。
- 已知 Return 應與其 Call、Task、attempt 或 child results 的依據一起驗證。只有部分輸出時，不得捏造完成 Receipt。
- Caller 可以等 Task、重新連回既有 Task，或取得最終 Return；「等待畫面中斷」不應偷偷建立第二個 Task。

### 尚未決定

- Common Return 採 shell-like status 加 streams，或 tagged `ok/error/unknown`；`unknown` 是否出現在 Return 外層仍待選。
- Repair 如何引用外部證據、如何標記已完成，以及明確「再做一次」是新 Task 還是受控新 attempt。

## 可相信的原型範圍

### 原型暫選

- [P1a-1 v2](../workbench/2026-08-14/p1a-task-tree-python-v2/README.md) 用 fake executor 驗到 Call、Task、parent relation 與 Receipt 可分開；known result 在重開後只補提交，不重做，缺完整結果則停在 repair／unknown。
- [P1a-2A](../workbench/2026-08-14/p1a2-process-python/README.md) 又驗到 root 接受、兩個 fake child 與固定持久順序可在 process-kill 切點後恢復成相同 bytes。
- 這些結果不固定 `Call`／`Return`／`Receipt` 公開名稱，也不提升 exact JSON、event、ID、CLI、容量或 `<store>` layout。

### 明確延後

- 跨機保留 live PID／attempt、network exactly-once、完整 GC，以及把任意 process heap、thread、fd 或 socket 做成可續跑 Task image。
