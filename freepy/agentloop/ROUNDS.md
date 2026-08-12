# Round 與 Step

這份定義 `agentloop` 對外的活動單位，以及 `run()`／`Handle` 的控制語意。

> **實作狀態：**以下的 Step/tool callback、公開可變狀態、`auto_finish`、`waiting`
> 與單一 parked runner 已落地。OS sandbox、worker pool 與 scheduler 不屬於本層。

## 固定術語

### 回合（Round）

從模型因一筆指令啟動，到控制者確定結束為止。一次回合可以包含很多步與工具呼叫。
模型不再呼叫工具只代表它自然靜止；是否同時結束 Round，由 `auto_finish` 決定。

使用者在回合尚未結束時可以直接存取或修改公開狀態。**修改資料不隱含繼續執行**；
是否繼續只由 callback 的控制結果與明確的 `pause()`／`resume()` 操作決定。

### 步（Step）

一次：

```text
ask(prompt / images / tools / tool_results / 追加指令 ...)
→ 模型推理
→ 模型產出一則新 message
```

模型推理與產出合計一步。工具執行發生在兩步之間，不另算一步；一步要求十個工具仍只算一步。

目前 API 已統一為 `Limits.steps` 與 `Handle.step`。原本存放工具執行紀錄的
`Handle.steps` 改為 `Handle.tool_log`，避免與 Step 混淆；舊名稱不保留相容 alias。

曾考慮把 Step 改稱 Action，但暫不採用：模型的一則 message 可能同時要求多個 tool actions，而工具執行本身也常被叫 action。`ask() → message` 用 Step 較不會和兩步之間的動作混淆。這只是命名決定，不改變「一個 Round 包含多個 Step」的實際分層。

## 一個典型回合

```text
使用者初始指令
  → Step 1：模型要求 read_file
  → tool：read_file 回結果
  → Step 2：模型要求 edit_file、run_test
  → tools：依序完成
  → 使用者追加「也檢查 Windows」
  → Step 3：工具結果 + 追加指令一起送入
  → tool：再執行
  → Step 4：模型回完成說明，不再呼叫工具
  → auto_finish=True：Round completed
    auto_finish=False：Round waiting，等外部修改或 resume
```

## 固定的 operation 與 callback 邊界

保留上述 Step 定義，不把工具執行塞進 Step。一次循環的順序固定為：

```text
Step N：ask(prompt / 上一批 tool results / 其他公開輸入)
  → 模型回傳 message，可能含 tool calls
  → 原子提交 Step 狀態
  → after_step callbacks
     callbacks 可增刪查改 message、tool calls 與其他公開狀態

執行 callbacks 修改後留下的 tool calls
  → 整批工具執行完成
  → 原子提交 tool results
  → after_tools callbacks
     callbacks 可增刪查改 tool results 與其他公開狀態

Step N+1
  → 將 after_tools 最後留下的 tool results 傳給模型
```

兩種 callback 都是安全邊界。控制結果至少分成：

- `CONTINUE`（數值 `0`）：進入下一個 operation。
- `PAUSE`：保留當前公開狀態，不開始下一個 operation。
- `END`：保留當前公開狀態並結束 Round。

同一邊界的 callbacks 依註冊順序全部執行，不能遇到第一個非零結果就短路；後面的
callback 看得到前面的修改，衝突採 last writer wins。全部完成後才彙總控制結果，
優先序是 `END > PAUSE > CONTINUE`。

`after_step` 回 `PAUSE`／`END` 時尚未執行該 message 要求的 tools；`after_tools` 回
`PAUSE`／`END` 時保留修改後的 results，但尚未送入下一個 Step。Limits、完成判斷、
tool 白名單、usage policy、result 過濾與審計都應由外部 callback 實作，而不是寫死在
agentloop 控制迴圈裡。

## Round 狀態機

```text
idle → ready → running_step → ready → running_tools → ready → ...
                       ↘                    ↘
                        waiting / paused

ready / running / waiting / paused → completed | error
```

`ready` 表示上一個 operation 已完整提交、下一個尚未開始；逐步 runner 會在這裡返回。
`waiting` 表示模型沒有要求工具、但 Round 仍活著；`paused` 表示外部控制者主動暫停。
三者都是安全邊界，保留可修改狀態；在 `ready` 呼叫 `pause()`／`end()` 會立即生效。
`completed` 才是正常且不可 resume 的完成狀態。

`error` 表示 endpoint、callback 或 runner operation 拋錯。自然完成和 callback 的
`END` 進入 `completed`；`Handle.stop` 仍保留 `done`、`length`、`budget`、
`input_tokens`、`output_tokens`、`engine` 等具體原因。

現在 `Handle` 就是一個 Round 的 one-shot 把手；若同一 bot 開新回合，必須建立
新 Handle，bot history 則可延續。重用或同時使用同一 Handle 會立即失敗；
同一 bot 也不要同時交給兩個 `run()`，因為兩邊會同時改同一份 history。
簡單說：等上一個 `run()` 結束，再開下一個。

`round_id` 與完整 event trace 仍延後；未來可記錄 started/finished、初始指令、
追加輸入與設定、Steps、tool calls、usage 與 stop reason。

## 公開可變狀態

Handle 應盡量直接暴露資料，讓外部控制者增刪查改，包括但不限於：message、
tool calls、tool results、history、prompt、images、tools、dispatch、ask options、usage、
Step 計數與 phase。原本的 `add_instruction()`、`add_images()`、`add_tools()`、
`set_ask_options()` queue API 不再是目標介面。

外部控制者承擔修改錯誤、內容不一致與安全 policy 的風險；agentloop 不因某項資料被
修改就自動多跑一步。跨執行緒要原子修改多個欄位或 nested list/dict 時，使用
不限制修改能力的 `with handle.edit(): ...` 交易邊界。不使用它仍可直接修改，但一致性
由操作者承擔。

## 自然靜止與 `auto_finish`

模型回傳的 message 沒有 tool calls 時，先完成 `after_step` callbacks，再進入自然靜止：

- `handle.auto_finish = True`：轉成 `completed`，`run()` 返回；預設維持現有行為。
- `handle.auto_finish = False`：轉成 `waiting`，`run()` 不因自然靜止而返回。

外部修改資料不會自動喚醒；必須明確 `resume()`。因此「模型沒有 tool calls」、
「`run()` 返回」與「Round completed」是三件可以分開的事。

## pause 與 end 的安全邊界

「安全邊界」的意思只是：**不要把一件已開始的事切成一半**。

- 模型正在產生一個 Step 時，合作式控制會等這次 `ask()` 自己收尾。
- 工具正在執行時，先跑完當前這批工具。
- `pause(safe=True)` 提出 cooperative pause request，在上述事情完成後進入 `paused`。
- `pause(safe=True)` 本身立即返回，不同步等待 runner，以免 callback 在 runner thread
  裡呼叫時自鎖；需要同步等待時另用 `wait_until_paused()`。
- `end(safe=True, reason="ended")` 使用相同邊界，但會永久完成 Round；已在
  `waiting`／`paused` 時立即結束並喚醒 runner。它也是立即返回的合作式請求。
- callback 用 `END`，外部 controller 用 `end()`；兩者最後都走相同的
  `completed` 狀態轉移。`auto_finish=True` 則是模型自然靜止時的自動結束。
- `ask_stop()` 不再是目標 API；要保留可恢復狀態使用 `pause()`，要永久結束使用
  callback `END` 或 controller `end()`。

如果工具已經執行，它的結果必須先登記回 bot history；否則下次開新 Round 時，
系統可能以為工具還沒跑過，把副作用重做一次。這就是為什麼 pause 不是立刻切斷。

Runner ownership、多執行緒與強制中止的契約另見 [RUNNER.md](RUNNER.md)。
