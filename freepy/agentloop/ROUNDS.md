# Round、Step 與工具輸入

這份定義 `agentloop` 對外的活動單位，並規劃目前 `run()`／`Handle` 要補的語意。

## 固定術語

### 回合（Round）

從模型因一筆指令啟動，到模型完成一連串工具動作、最後主動不再呼叫工具而停止。一次回合可以包含很多步與工具呼叫。

使用者在回合尚未結束時追加指令，指令會在下一步送入，回合繼續；不另開新回合。若完成狀態已經原子確立，之後的新指令才開下一回合。

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
idle → thinking → running_tools → thinking ... → completed
             ↘ awaiting_tool_input ↗
任意 active state → paused → 原狀態
任意 active state → stopped | budget | error | length
```

未來每回合產生不可重用的 `round_id`，記錄 started/finished、初始指令、追加指令、Steps、tool calls、usage 與 stop reason。`Handle` 是一個 Round 的把手；若同一 bot 開新回合，應建立新 Handle，bot history 則可延續。

## 回合內追加指令

`Handle.say(text)` 改名或補一個語意更清楚的 alias：

```python
handle.add_instruction(text)
```

規則：

- 不打斷正在進行的模型 request 或工具。
- 排入 FIFO，在下一次 `ask()` 合併送入。
- 若模型本步沒有 tool calls，但 queue 已有追加指令，不能先結束回合。
- completion 判斷與 enqueue 必須共用 lock，解決「最後一步剛回來、使用者同時插話」的 race。
- completion 已提交後才到達的指令，明確拒絕並回 `round already completed`，由 supervisor 開新 Round；不能安靜遺失。

`quiet > 1` 的 nudge 也算 supervisor 對同一回合追加的控制指令。預設 `quiet=1` 時，一則完成且無 tool calls 的模型 message 就表示主動靜止。

## 工具內部要求使用者輸入

這不是新指令、不是新步驟，也不直接寫成 LLM user message。它不另外「延長一個回合」；原回合本來就要等這次工具呼叫完成。工具只是進入可觀察的等待狀態：

```python
value = tool_context.request_input(
    prompt="選擇部署環境",
    choices=["staging", "production"],
    secret=False,
    timeout=300,
)
```

supervisor/UI 收到 `ToolInputRequest(request_id, tool_call_id, ...)`，再用獨立入口回覆：

```python
handle.provide_tool_input(request_id, value)
```

只有 request id 相符的等待工具能取走輸入。一般 `add_instruction()` 不會誤送給工具，`provide_tool_input()` 也不會進 bot history。工具完成後，其最終結果照常成為下一步的 tool result。

敏感輸入可標 `secret=True`：不寫 event payload、不出現在 `now()`、不自動包含於 tool result；是否傳給外部程序由該工具自己的 policy 決定。

## 為什麼這不等於 wait_for_message

communication 的 `wait_for_message` 是 agent 等另一個 agent，可能無期限閒置；仍不提供。

tool input 是一筆已在執行中的 tool call 主動產生、帶 request id、prompt、timeout 與取消語意的明確等待。`Handle.now()` 能顯示 `awaiting_tool_input`，所以外部可區分等待與卡死。

## 預算與停止

- 一次 tool input 不增加 Step，也不增加 tool call 數。
- 等待時間是否計入 Round wall-clock limit 要明確配置；預設仍計入，避免無限掛住。
- tool 自己可有 input timeout；timeout 後回一般工具錯誤字串，模型下一步可改做法。
- `ask_stop()` 必須喚醒等待中的工具並送 cancellation；不能只改 flag 留下 blocked thread。
- `pause()` 不取消工具，也不攔工具輸入；它只阻止下一步模型 request。

## 要改的既有位置

1. `Handle`：`round_id`、thread-safe instruction queue、tool input request/response、active state lock。
2. `loop._settle()`：傳入 `ToolContext`，辨識 awaiting input，停止時取消。
3. `loop.run()`：沒有 tool calls 時先原子檢查 instruction queue，再 commit completion。
4. `calling.perform()`：保留現有普通 function，相容可選的 context-aware tool protocol。
5. `__main__.py`：補回合內最後邊界插話、工具輸入、timeout、secret、stop cancellation 測試。
