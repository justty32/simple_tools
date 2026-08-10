# Swarm 與 Instructor

兩者都包在模型 API 外圍，但位置完全不同：Swarm 管「接下來由哪個 agent/tool 做」，Instructor 管「回來的資料是否符合契約」。

## Swarm：很小的 handoff loop

Swarm 的 `Agent` 只含 name、model、instructions、functions 等欄位；`Result` 可同時回傳文字、下一個 agent、context updates（[types.py](https://github.com/openai/swarm/blob/6af0b4caf37dca4526dfd98e9fbd8ce36e7eeb22/swarm/types.py#L14-L41)）。主迴圈：

1. 以 active agent 的 system instructions 呼叫模型；
2. append assistant message；
3. 順序執行 tool calls；
4. 合併 context，必要時換 active agent；
5. 沒有 tool calls 時返回（[core.py](https://github.com/openai/swarm/blob/6af0b4caf37dca4526dfd98e9fbd8ce36e7eeb22/swarm/core.py#L252-L291)）。

好處是 caller 明確持有 messages、agent、context，框架本身不假裝有持久記憶。`execute_tools=False` 也提供一個乾淨的執行中斷點。

### 值得參考

- handoff 是普通 tool result，不需要另一套神秘 routing runtime；
- 換 agent 時替換 system prompt，但保留共同 history；
- 找不到工具時把錯誤作為 tool result，讓模型可恢復；
- context variable 可注入函式，卻不暴露為模型可填參數。

### 不照搬

- README 已明確標示 educational/experimental，production 建議改用 Agents SDK。
- `Result` 把輸出、state mutation、handoff 混在一起；freepy 應拆成 typed transition。
- handoff 沒有 grant/budget/ancestry 檢查，不能等同 `spawn_agent` 或 team mutation。
- `max_turns` 的 while condition用新增 history 長度計數；tool messages 也會影響它，與 freepy 的 Round 不同。
- model 可請求 parallel tool calls，但 handler 仍用 `for` 順序執行；不要從 schema 推論並行保證。

## Instructor：輸出契約編譯器

Instructor 不是 agent framework。它把 provider client 包成：normalize response model → request handler → provider → response parser → validation → 必要時 re-ask。`ModeHandlers` 將 request、reask、response、stream、message 與 template 的 provider 差異收在 registry（[registry.py](https://github.com/567-labs/instructor/blob/6754a32b1e35d57dfd94aea8099be68478f1e133/instructor/v2/core/registry.py#L46-L89)）。

`prepare_response_model` 能把 TypedDict、`list[T]`、scalar 等統一成可驗證 schema（[response_model.py](https://github.com/567-labs/instructor/blob/6754a32b1e35d57dfd94aea8099be68478f1e133/instructor/v2/core/response_model.py#L38-L111)）。validation/JSON parsing 失敗時，retry loop 保存 failed attempt、產生 provider-specific re-ask、累加 usage，再呼叫模型（[retry.py](https://github.com/567-labs/instructor/blob/6754a32b1e35d57dfd94aea8099be68478f1e133/instructor/v2/core/retry.py#L270-L409)）。

### 值得參考

- freepy task report、tool args/result、agentfs JSON view 都應 schema-first；
- provider-specific shaping 放 handler registry，不污染核心 state machine；
- validation failure 是有證據的控制流，保留 raw response、attempt、usage；
- hooks 事件名固定，可轉成 append-only trace。

### 不照搬

- 不直接 monkey-patch provider client；freepy 用顯式 adapter/executor。
- 不搬 40+ mode/provider 相容矩陣；只做實際需要的 modes。
- re-ask 每次都是真正模型呼叫。在 freepy 中每次都要增加 `round_no`、usage 與 budget，不能因外部 `create()` 只呼叫一次就隱藏。
- schema retry 僅能重試模型解析；有副作用的 tool mutation 不能跟著無條件重做。
- audit/security hook 失敗要 fail closed，不能只 warning 後繼續。

## 合併後的好切片

Swarm 的 `Result(agent=...)` 改為 Instructor-style discriminated schema，例如 `HandoffIntent(target_path, reason, task_ref)`；驗證成功後仍須由 supervisor 檢查 path、grant、budget、instance 與 policy，最後才產生 `HandoffReceipt`。模型選擇的是請求，不是權限。
