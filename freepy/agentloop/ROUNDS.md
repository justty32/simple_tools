# Round 與 Step

這份定義 `agentloop` 對外的活動單位，以及 `run()`／`Handle` 的控制語意。

## 固定術語

### 回合（Round）

從模型因一筆指令啟動，到模型完成一連串工具動作、最後主動不再呼叫工具而停止。一次回合可以包含很多步與工具呼叫。

使用者在回合尚未結束時追加輸入或更改設定，變更會在下一步生效，回合繼續；
不另開新回合。若完成狀態已經原子確立，之後的變更才由 supervisor 開下一回合。

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
  → Round completed
```

## Round 狀態機

```text
idle → active（thinking ↔ tool）→ completed
                 ↕
               paused

active → error
```

`completed` 是正常完成。`paused` 不是結束，它保證還能 `resume()` 回 active。

`error` 是所有非正常終止的總類，包括 `stopped`、強制中止、`budget`、`length`、
端點錯誤、引擎錯誤與其他預算用完。進入 error 後，這個 Round 已結束，
不保證能繼續。`Handle.stop` 仍保留具體原因，方便知道是哪一種 error。

現在 `Handle` 就是一個 Round 的 one-shot 把手；若同一 bot 開新回合，必須建立
新 Handle，bot history 則可延續。重用或同時使用同一 Handle 會立即失敗；
同一 bot 也不要同時交給兩個 `run()`，因為兩邊會同時改同一份 history。
簡單說：等上一個 `run()` 結束，再開下一個。

`round_id` 與完整 event trace 仍延後；未來可記錄 started/finished、初始指令、
追加輸入與設定、Steps、tool calls、usage 與 stop reason。

## 回合內重新配置下一個 Step

Round 途中有很大的操作空間；當前 operation 不切斷，下一個 Step 可以收到新的
輸入、能力與 `ask()` 選項：

```python
handle.add_instruction(text)
handle.add_images("screen.png", "https://example.com/photo.jpg")
handle.add_tools(schemas, dispatch)
handle.set_ask_options(tool_choice="required", stream=True)
```

規則：

- 不打斷正在進行的模型 request 或工具。
- 文字與圖片排入 FIFO，只送下一次 `ask()`；目前多媒體 adapter 只支援圖片。
- tool schema 與同名 dispatch 必須一起提交，從下一個 Step 起持續可用；同名就是替換。
- ask options 從下一個 Step 起持續生效，傳 `None` 清除。`stream=True` 仍由阻塞的
  `run()` 收完，不表示 agentloop 會向外廣播 chunks。
- `prompt`、`images`、`tool_results` 與 `remember` 是 loop 維持 history 協定所需的
  欄位，不能從 ask options 覆蓋。
- 若模型本步沒有 tool calls，但 queue 有任何變更，不能先結束回合。
- completion 判斷與 enqueue 必須共用 lock，解決「最後一步剛回來、使用者同時插話」的 race。
- completion 已提交後才到達的變更，明確拒絕並回 `round already completed`。
- stop 已接受後的新變更回 `round is stopping`；已排隊但尚未送出的變更不越過 stop。

`quiet > 1` 的 nudge 也算 supervisor 對同一回合追加的控制指令。預設 `quiet=1` 時，一則完成且無 tool calls 的模型 message 就表示主動靜止。

## pause / 合作式 stop 的安全邊界

「安全邊界」的意思只是：**不要把一件已開始的事切成一半**。

- 模型正在產生一個 Step 時，合作式控制會等這次 `ask()` 自己收尾。
- 工具正在執行時，先跑完當前這批工具。
- `pause()` 在上述事情完成後暫停，所以之後可以從明確的位置繼續。
- `ask_stop()` 也先等當前事情收尾，然後將 Round 結束為 error，不再開始新工作。

如果工具已經執行，它的結果必須先登記回 bot history；否則下次開新 Round 時，
系統可能以為工具還沒跑過，把副作用重做一次。這就是為什麼 stop 不是立刻切斷。

## operation 在中途被強行中止

這種中止只結算當前 Step 或 tool call，不等於殺掉整個實例。

### Step 中斷

Step 的邊界就是 `ask() → message`，不需要 agentloop 猜 HTTP request 到了哪裡：

- `Reply` 沒有留下任何 message：llms 回滾這次新增的 history，agentloop 不計 Step。
- `Reply` 已留下完整或部分 message：保留 message，agentloop 計一個完成的 Step。
- 部分 message 可以同時帶 `reply.err`；這時 Step 已完成，但 Round 仍以 error 結束。

串流 `Reply` 已負責累積片段、提前 `close()` 與「完全落空就回滾」。agentloop 若把
`stream=True` 設為 ask option，仍會同步收完整個 Reply 後才跨過 Step 邊界。

### tool call 中斷

當作這次工具執行失敗，產生一筆錯誤 tool result 並照常回填 history。
若 Round 本身沒有被中止，模型下一個 Step 可以根據這筆錯誤改做法。工具在被中斷前
已產生的外部副作用仍可能存在，錯誤結果不代表自動 rollback。

工具必須支援取消，或在可終止的獨立 process 裡執行，才能真正實現中途中斷。

## 整個實例被強制終止

Ctrl-C、SIGTERM 或直接殺 process 屬於不可抗力。這種情況不做收尾保證：
當時已經保存什麼就留下什麼，尚未保存的狀態可以遺失。
