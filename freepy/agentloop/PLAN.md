# agentloop 待完成項目

日期：2026-08-10

## 結論

`agentloop` 不需重寫。現在只收尾控制邊界、`Handle` 跨 thread、錯誤收尾與限制語意。

本次**不動根基**：llms、proxy、tooljson 介面 freeze。Plan 9、多 agent、memory、runtime、固化等都不是 v1 前置條件。

完成後仍然只有這個形狀：

```python
h = await agentloop.run(bot, dispatch, prompt, handle, limits)
```

`bot` 只需同步 `ask()` 與 `pending_calls`；`dispatch` 仍是 `{name: fn(**args)}`。工具依序執行；agentloop 不 import llms 或 tooljson。

## 命名

```text
Round      一次完整 run()
Step     一次 ask()，到完整收到 message 或 endpoint error
tool call 發生在 Step 之間，不另算 Step
```

request 送出即計一個 Step；timeout/error 也算，transport 內部 retry 是另一個 attempt
維度，不另算 Step。API 現已使用 `Handle.step` 與 `Limits.steps`；原本佔用 `steps`
名稱的工具紀錄已改成 `Handle.tool_log`，且不保留舊名稱。完整定義以
[ROUNDS.md](ROUNDS.md) 為準。

## v1 範圍

### 這次完成

- ask → tools → results → ask、pending debt 與不重複續跑。
- stop reasons、各種 loop/tool/engine limits、quiet。
- 工具參數預檢、錯誤字串化、輸出截斷。
- `Handle` 查狀態、追加指令、pause/resume/stop；可從其他 coroutine/thread 操作。
- 離線驗證為 gate；真模型只作額外 smoke。

### 明確延後

- tool input、`round_id`、event trace、跨 process control plane。
- async/parallel tool、streaming loop、retry、OS sandbox。
- 多 agent、mailbox、team、memory、agentfs。

現在沒有工具使用 tool input，不為假 consumer 同時擴張 Handle、calling、secret 與 cancellation。

## 現況基線

2026-08-10 在 current worktree 重跑：

```text
PYTHONPATH=llmkit uv run python -m agentloop
```

現有 **35 個離線關卡全過**，覆蓋基本迴圈、多工具、工具壞法、limits、quiet、控制與 pending debt。它們尚未精確證明 pause/stop/追加指令的生效邊界。

## 實證出的完成缺口

### P0：最後一個 Step 會吃掉同時到達的追加指令

模型回答中 `say("late")`，若回覆沒有 tool calls，現況直接 `done`；實測 `late` 留在死 queue。

要求：enqueue 與 completion commit 共用 lock。無工具時先原子檢查 queue；有指令就開下一
Step，空才 `done`。提交後追加明確回 `round already completed`。

### P0：tool 中的 pause／stop 會多放行一個 Step

tool 內 `pause()` 仍會立刻進下一次 ask，甚至留下 `paused=True` 的已完成 Handle；`ask_stop()` 也可能得到 `done`。原因是 `_settle()` 後沒有 control checkpoint。

要求：每批 tools 結束後、下一次 ask 前重新檢查 pause/stop。pause 必須真的擋住下一次 ask；stop 必須成為最終 stop reason。

必要例外：tool batch 中 stop 時，結果要先用一個 settlement Step 餵回 bot，否則續跑會重做 history 中仍 pending 的副作用：

```text
stop during model → 收完本 Step；不執行新 tool calls；stopped
stop during tools → 跑完整批 → 送回全部 results 一個 Step → 不執行新 calls；stopped
pause during tools → 跑完整批 → 暫停在送 results 前；resume 後再 ask
```

一批工具是不可切割的 settlement unit，以保住 pairing 並避免副作用重跑。

### P0：generic bot 拋例外會繞過 Handle

`LLM.ask()` 保證回 Reply，但 duck-typed bot 未必如此。實測 raw `RuntimeError` 會逃出 `run()`，Handle 留在 `thinking`、`stop=None`。

要求：bot/Reply 邊界的非取消例外收成 `h.end("error", err=e)`；工具例外仍由 `perform()` 字串化。

### P1：Handle 的 thread 承諾尚未成立

文件允許其他 thread 控制，但 phase、flags、`Handle.tool_log`、`_say` 都是裸 field/list；GIL 不等於完成/enqueue 原子性。

要求：用 `threading.Lock` 保護 lifecycle、flags 與 instruction FIFO。live 狀態只透過 `done()`／`now()`／控制方法保證一致；公開結果在完成後穩定。Handle 是 one-shot，重用/同時使用 fail fast。

### P1：Limits 缺少輸入契約

現有 `Limits(steps=None)` 直到 `range(None)` 才炸；負數、錯型別也延後，`quiet=0` 還被無聲改成 1。

要求：建構時驗證。`steps` 是正整數；`calls`、`seconds`、`tokens` 是 `None` 或非負值；per-tool cap 非負；`quiet >= 1`。錯誤直接 `ValueError`。

`seconds`／`tokens` 是 **cooperative limit**：只在安全邊界檢查，可超過一個 model/tool operation，並非 hard kill。

## v1 控制語意

### Round 與 Step

- 一次 `run()` 是一個 Round；一次 endpoint request/response 是一個 Step。
- tools 在兩個 Step 之間執行，不增加 Step。
- 一個 Handle 只屬於一個 Round；同一 bot 可在下一 Round 用新 Handle 延續 history。
- 同一 bot 同時跑兩個 Round 不支援；budget/stopped 後再次 `run()` 是新 Round。

### 安全邊界

控制只在不會製造半包 results 的位置生效：

1. 開始一批 pending tools 前。
2. 完整跑完一批 tools、下一次 ask 前。
3. 模型回覆完整寫入 history 後、開始新 tools 或提交完成前。

不取消 HTTP、不切斷已開始的 tool batch、不撤銷副作用。

### 優先順序

同一邊界同時發生時：

```text
error > stop > pause > queued human instructions > pending tools > quiet nudge > done
```

human instruction 存在時不另外塞 nudge，且不增加 `quiet`。stop 一旦接受，之後的新 instruction 直接拒絕。

### 計數

- `step`：真的發出的 model request 數。
- `calls`／`used`：通過 policy、交給 `perform()` 的 attempts；missing handler/bad args 也消耗一次，避免繞過預算。
- allowlist/per-tool 擋下的 call 留 `tool_log`、回錯誤字串，但不增加 calls/used。
- tokens 只加 provider 回報的 `usage.total`；工具自己呼叫模型不在此帳。

## 實作順序

1. **先補 deterministic regression tests**：用 `threading.Event`／async event 控制抵達時點，不靠碰運氣的 sleep。
2. **Handle + 命名**：step/tool_log、lock、instruction FIFO、完成提交、one-shot guard。
3. **重排 loop checkpoints**：將 settlement、控制、ask、completion 分成明確小步驟。
4. **exception normalization**：generic bot failure 一律落到 Handle。
5. **Limits validation**：建構時 fail fast，文件改成 cooperative limit。
6. **整理文件**：README 與規劃統一採用 `ROUNDS.md` 的 Round／Step；OS sandbox 不實作。

不新增第三方依賴，不改 llms/tooljson，不趁機抽 framework。

## 驗收關卡

- 現有 35 個離線關卡維持全過；因修正 stop 語意而改 expectation 必須在 diff 說明。
- final-response 與 instruction 同時抵達：instruction 被下一 Step 看見，或在 completion 提交後明確拒絕。
- 多 thread enqueue 保持 FIFO；不丟、不重複。
- pause during tool：resume 前 ask count 不增加。
- stop during model：新 tool calls 不執行；stop=`stopped`。
- stop during tool batch：每支只執行一次，results 配對完整，settlement 後不執行新 calls，stop=`stopped`。
- raw bot exception：`run()` 正常回 Handle，stop=`error`，phase 不殘留 thinking/tool。
- Handle 重用、非法 Limits 立即失敗，沒有半啟動 Round。
- pending debt 原有續跑案例維持不重複。
- 測試全離線；另選一顆穩定 tool model 跑一次 live smoke，只作人工確認。

## 請審核的三個決定

1. **tool input 延後**：本次不實作 `ToolContext`／secret／timeout/cancel channel。
2. **stop during tools 要有 settlement Step**：為了不重跑副作用，允許多一次模型 request，但不再執行它新要求的工具。
3. **控制 API 保持小**：保留 `say/pause/resume/ask_stop`；可只補 `add_instruction = say` 的清楚 alias，不新增 event bus 或 supervisor API。

這是一次性交審檔；完成後把決定折回 README，再刪除本檔。
