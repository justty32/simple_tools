# 映射到 freepy／agentfs

外部專案多以 Python object 或 graph 為中心；freepy 的差異是把 agent 身分、組織與 live state 投影成 logical path。這個方向沒有被七者推翻，反而可用它們的事件與 persistence 經驗補強。

## 對照表

| 外部做法 | freepy 落點 | 必要修正 |
|---|---|---|
| Swarm handoff | team/runtime typed request | target path 不等於授權；回 receipt |
| Instructor schema/re-ask | llm adapter、task report、tooljson | 每次 re-ask 算新 Step |
| MiniChain lazy graph | workflow spec / context compiler | per-run context，禁止 global state |
| PocketFlow lifecycle | operation node | effect 與 post receipt 分離 |
| LightAgent hooks/review | policy middleware/audit | security hook fail closed |
| CrewAI Task/Flow | team task + workflow run | 不搬 role-play runtime/threads |
| LangGraph checkpoint | runtime saver/event store | 外部 effect 用 outbox/idempotency |

## Round、Step、workflow node、attempt

現有 [Round/Step 定義](../../../freepy/agentloop/ROUNDS.md) 比外部框架更清楚，應保持：

- 使用者啟動到 agent 主動停止是一個 Round；
- 每次真正 provider ask→message 是一個 Step；
- Instructor validation re-ask 是新 Step；
- Swarm loop 的 history message counter 不是 Step；
- LangGraph superstep、PocketFlow node、CrewAI task 都是 workflow step，可能含零到多個 Step；
- retry attempt 是另一維度，不因重試便偷改 Round/task identity；
- tool 內部 human input 仍是 pending tool event，不增加 Step。

每個 event 同時可帶 `round_id, step_no, workflow_run_id, workflow_node_no, task_id, tool_call_id, attempt_no`，不用硬把它們壓成一個數字。

## agentfs 應投影什麼

在既有 [agentfs 規劃](../../freepy/future/agentfs/PLAN.md) 上增加 read-only views：

```text
/root/build/.agent/
  state
  round
  step
  events
  runs/<run-id>/{state,step,checkpoint,interrupts}
  tasks/<task-id>/{status,brief,report}
  effects/<effect-id>/{intent,approval,receipt}
```

authoritative state 仍屬 runtime/team/memory/workflow store；agentfs 只是 per-actor projection。寫 `.agent/ctl` 時也只能轉成相同 typed operation，不能直接改 checkpoint JSON 或 permission file。

## 組織化記憶

外部框架常把 recall 結果直接拼回 prompt。freepy 應採較嚴格組合：

1. Instructor 式 schema 驗證 memory metadata；
2. LightAgent 式 candidate→promotion 與 trust/scope/expiry；
3. CrewAI 式明確 prompt-build extension point；
4. 每個 Step 保存實際 context manifest；
5. memory ref 仍做 ACL 檢查，知道 ref/path 不等於獲准讀取。

checkpoint 只保存 workflow cursor/state ref，不複製整份模型 history。source trace、memory object、Step manifest 與 checkpoint 可以互相連結，但生命週期各自獨立。

## Runtime 與安全

七個框架的 schema、guardrail、router、checkpoint 都是應用層技術，不能限制 process 真正的檔案/網路/CPU 權限。[agent runtime 規劃](../../freepy/future/agent-runtime/PLAN.md) 的 supervisor、effective policy、rootless container/cgroup 與無 runtime socket 原則仍需保留。

正確效果路徑是：

```text
model/node -> EffectIntent -> schema/grant/policy/approval
           -> sandbox executor -> EffectReceipt -> event/checkpoint
```

這也是 Plan 9/Unix 風格的接口分離：graph 決定何時求值，capability 決定能否求值，executor 才真正造成副作用。
