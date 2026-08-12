# Runner ownership 與中止

本頁接續 [ROUNDS.md](ROUNDS.md)，只談誰能推進 Handle、parked runner 與無法合作式收尾的錯誤。

## 多執行緒原則

一個 Handle 同時只能有一個 runner thread；只有 owner runner 能執行 `ask()`、tools 與狀態
轉移。callbacks 由 runner thread 依註冊順序同步呼叫，不另開 thread。任意數量的 controller
threads 可以透過 Handle 觀察、修改、pause 或 resume。

agentloop 核心以單執行緒執行為主要設計目標：一條 runner thread 從頭到尾擁有同一個 Round。
核心不建立 thread；可選的 `agentloop.threading.start()` 只包裝一條背景 thread，不改變控制
語意。要建立多少 threads，以及 parked thread 的資源成本，仍由使用者或上層 runtime 負責；
agentloop 不提供 worker pool 或 scheduler。

同步必須處理 pause/下一個 operation、waiting/resume、`auto_finish` 切換、callback 清單修改、
tool calls/results 修改及 runner ownership 等競態。等待使用 `threading.Condition` 的 predicate
loop；`resume()` 必須在同一把 lock 下改狀態並 `notify_all()`，避免 lost wakeup。每次通知
callbacks 前先取得 callback 清單快照。

callback 要能重入 Handle，因此 callback 在 `threading.RLock` 下執行。遵守 `handle.edit()` 的
controller 會等當批 callbacks 全部完成；未使用 `edit()` 的 nested mutable state 修改，其一致性
由外部控制者承擔。

runner 生命週期採 **parked runner**：進入 `waiting` 或 `paused` 時，本次 `run()` 不返回，原
runner 停在 Condition 裡；`resume()` 喚醒同一條 thread 繼續。只有 Round 進入
`completed`／`error` 或 callback 回 `END` 時，`run()` 才返回。這讓 ownership 保持簡單；長期
占用 thread 是呼叫者明知並自行承擔的成本。

逐步 `advance()` 也保留同一 owner thread。owner 在 `waiting`／`paused` 呼叫時是不阻塞的 no-op；
其他 thread 若嘗試 `advance()`，會以 ownership error 拒絕，不能重複執行 model request 或 tools。

## operation 在中途被強行中止

這種中止只結算當前 Step 或 tool call，不等於殺掉整個實例。

### Step 中斷

Step 的邊界就是 `ask() → message`：

- `Reply` 沒有留下任何 message：llms 回滾新增 history，agentloop 不計 Step。
- `Reply` 已留下完整或部分 message：保留 message，agentloop 計一個 Step。
- 部分 message 可同時帶 `reply.err`；Step 已完成，但 Round 仍以 error 結束。

串流 `Reply` 負責累積片段、提前 `close()` 與「完全落空就回滾」。agentloop 即使設定
`stream=True`，仍同步收完整個 Reply 後才跨 Step boundary。

### tool call 中斷

當作工具失敗，產生錯誤 tool result 並照常回填 history。已產生的外部副作用仍可能存在，錯誤
結果不代表 rollback。工具必須支援取消，或在可終止的獨立 process 裡執行，才能真正中止。

## 整個實例被強制終止

Ctrl-C、SIGTERM 或直接殺 process 屬於不可抗力。這種情況不做收尾保證：當時已保存什麼就留下
什麼，尚未保存的狀態可以遺失。
