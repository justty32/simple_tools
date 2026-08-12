# 採用順序與驗收

原則：先讓既有 Round/tool loop 可觀測、可驗證，再加入 durable workflow；不要先造 generic graph DSL。

## Phase 0：凍結共同語彙

定義 versioned records：

- `ModelAttempt`、`RoundRecord`；
- `ToolCallIntent`、`PolicyDecision`、`ApprovalReceipt`、`ToolReceipt`；
- `WorkflowRun`、`StepTask`、`PendingWrite`、`Checkpoint`、`Interrupt`；
- 全部帶 actor path + instance id、operation id、timestamps、parent refs。

Gate：同一 trace 能回答「哪個 agent、哪個 Round/Step、哪個 step/attempt、看見什麼、要求什麼、實際做什麼」。

## Phase 1：結構化模型邊界

採 Instructor 的窄切片：response model normalization、provider handler registry、parse/validation、bounded re-ask、raw response + usage。

Gate：invalid JSON/schema 產生新 `ModelAttempt` 和新 Step；budget 正確累加；無 usage 不當成零；provider 差異只在 adapter。

## Phase 2：可審計工具閘門

採 LightAgent 的 phase hooks/approval receipt，加現有 tooljson：

```text
intent -> schema -> effective grant -> policy -> approval?
       -> executor -> receipt
```

Gate：approval 綁 tool/args hash/actor/instance；重放不同 request fail closed；hook/error 事件完整；真正 effect 只在 sandbox executor。

## Phase 3：最小 durable workflow

以 PocketFlow 的 lifecycle/action edge 作表面語意，以 LangGraph 的 checkpoint/pending write 作底層：只支援 sequential、branch、subflow、bounded retry、interrupt/resume。

Gate：

- state field 預設 single-writer；多寫未宣告 reducer 就失敗；
- `(checkpoint, task, write_index)` 唯一；重試不重複合併；
- crash 後能從 SQLite 恢復 pending interrupt；
- resume 重跑 node 時，effect executor 以 idempotency key 回既有 receipt；
- sync/async durability 是顯式 policy，不由框架暗選。

## Phase 4：task 與 handoff

採 CrewAI Task 的 input/context/tools/output contract、Swarm 的 explicit handoff intent，但接 freepy team/runtime：

- handoff：同一 agent role/context 切換；
- delegate：建立 team task；
- spawn：runtime 建 child instance；
- 三者使用不同 typed operation。

Gate：模型不能靠回傳 agent 名稱擴權；path/instance/grant/budget 全檢查；task 可跨 Round；通知失敗不丟 authoritative task。

## Phase 5：memory 與 agentfs projection

把 run/checkpoint/interrupt/effect/task event 投影到 `.agent/`；memory 只連 ref。先 Python resolver/tools，後決定 FUSE/9P。

Gate：per-actor view、snapshot generation、bytes/list limit、secret filtering；所有 projection read-only，`ctl` 只轉 typed command。

## 暫緩項目

- generic Pregel/graph DSL、parallel fan-out；
- CrewAI 式完整 role/manager runtime；
- LightAgent ToT、LLM substring router；
- MiniChain Python/Bash backend；
- remote telemetry/control plane；
- 未有 effect/outbox 前的自動 retry/resume。

## 成功標準

不是「支援七個框架 API」，而是同一組小 records/protocol 能表達它們最好的語意，同時保留 freepy 的 Round/Step、agent path、capability、memory refs 與 OS isolation。
