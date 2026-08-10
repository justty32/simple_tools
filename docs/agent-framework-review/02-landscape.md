# 七個框架的定位比較

## 它們解的不是同一題

| 專案 | 最小核心觀念 | 狀態模型 | 最值得取用 | 不應期待 |
|---|---|---|---|---|
| Swarm | Agent + handoff | caller 傳入 history/context | 極簡多 agent loop | durability、隔離、授權 |
| Instructor | response schema + re-ask | call/attempt metadata | 結構化輸出邊界 | team/workflow runtime |
| MiniChain | lazy Chain + backend | global context + cache | 延遲求值與 snapshot | 並行安全、現代依賴 |
| PocketFlow | Node + action edge | shared mutable dict | lifecycle、subflow | persistence、policy |
| LightAgent | 單體 Agent runtime | mutable instance state | hooks、review、trace、memory policy | 分散式 swarm、OS sandbox |
| LangGraph | StateGraph + Pregel | channel/reducer + checkpoint | durable step、interrupt、pending writes | 自動替你解決副作用冪等 |
| CrewAI | Agent/Task/Crew + Flow | crew context + flow state | task contract、autonomy/control 分層 | 小依賴面、Plan 9 namespace |

## 四條不同的軸

### 1. 模型輸出是否可信

Instructor 把答案視為待驗證資料；這是最適合 freepy 的態度。Swarm、CrewAI 的 agent routing 仍可能由模型決定，所以 handoff 只能產生 intent，不能直接擴權。

### 2. 控制流在哪裡

- Swarm：模型呼叫 transfer tool。
- PocketFlow：`post()` 回 action，沿 edge 前進。
- CrewAI Flow：listener/router event。
- LangGraph：channel update 觸發下一個 Pregel superstep。

這些都可表達 branching，但只有 LangGraph 把 checkpoint/pending write 提升為一級契約；PocketFlow 的 100 行核心則最容易看清語意。

### 3. 狀態是否 durable

「有 dict」不等於 durable。PocketFlow shared dict、MiniChain global context、LightAgent in-memory trace 都適合單程序；LangGraph checkpointer 與 CrewAI Flow persistence 才開始處理 resume。即便如此，外部 API、檔案與 shell 副作用仍需 idempotency key/receipt。

### 4. 安全邊界在哪裡

七者都不是 container/cgroup/seccomp 的替代品。Guardrail、schema、graph edge 只限制軟體控制流；真正檔案、網路、process 權限仍應由 freepy supervisor + sandbox backend 強制。

## 對 freepy 的選擇

不要選「一個框架作底座」，而是分層取樣：

```text
data contract     <- Instructor
Round tool loop    <- freepy 現有 agentloop
workflow kernel   <- PocketFlow concepts + LangGraph durability
policy/audit      <- LightAgent concepts
handoff/task      <- Swarm + CrewAI concepts
namespace/view    <- freepy agent path + agentfs（外部專案都沒有）
OS enforcement    <- freepy runtime/container adapters（外部專案都不能代替）
```

這會保留 Plan 9／Unix 的小介面精神：每層只提供可組合 protocol，而不是讓一個 Python object 同時擁有 prompt、memory、tool、team、workflow、security 與 storage。

## 命名必須隔離

建議事件欄位固定為：

| 名稱 | 單位 |
|---|---|
| `round_id` | agent 從啟動到主動停止 |
| `step_no` | 一次 provider ask→message |
| `workflow_run_id` / `workflow_node_no` | deterministic workflow 的 run/node 或 superstep |
| `task_id` | team 層可跨 Round 的工作 |
| `tool_call_id` | 一次工具要求與結果配對 |
| `attempt_no` | 同一操作的 retry/re-ask 次數 |

框架內部叫什麼不重要；進入 freepy event schema 時必須轉成這組不含糊的語意。
