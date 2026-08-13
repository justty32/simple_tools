# Agent Machine

**狀態：研究底稿，實作順序已由
[`../../agent-machine/`](../../agent-machine/README.md) 接手。** 曾建立一份不完整 M0 骨架，現停放在
[`freepy/prototypes/agent_machine_m0/`](../../../../freepy/prototypes/agent_machine_m0/README.md)；資料
邊界的歷史方案保留於此。這層設想把既有 `agentloop`、runtime、team、memory
與 agentfs 組成一台 domain-specific machine：Linux 管實體資源，Agent Machine 管 endpoint、
token、context、goal 與 tool effect。

若你熟悉 C++／Go／Lua，但不熟悉這裡借用的 OS、分散式系統與 Lisp 術語，可先從
[HTML 跨領域導讀](../../../guides/agent-machine/index.html) 建立心智模型，再回來查精確規格。

詳細文件：

- [MODEL.md](MODEL.md)：慢速、機率性求值模型與 goal completion。
- [RESOURCES.md](RESOURCES.md)：從 OS 沿用的資源分類、排程、pressure 與回收規則。
- [PLAN9-LISP.md](PLAN9-LISP.md)：Plan 9 namespace 與 Lisp machine／condition system 的用法。
- [PLAN.md](PLAN.md)：package 邊界、實作里程碑與驗收。
- [SOURCES.md](SOURCES.md)：一手資料與本規格的推論邊界。

## 核心命題

```text
一台慢時鐘、機率 ALU、持久狀態、capability I/O 的 Lisp machine
```

一個 Step 要數秒至數十秒，因此 control plane 可以在每一步完整 serialize、validate、
account、snapshot 與 audit。這裡可犧牲的是相對便宜的本機控制面延遲，不是 token、費用、
endpoint slot 或人類等待時間。

思考引擎不是可靠 CPU。相同輸入只能得到另一個 sample，而且 engine 可能沒有完成 goal，
卻正常回傳甚至聲稱完成。runtime 因而管理的是 proposal 的產生與提交，而不是假設模型
直接執行了正確 instruction。

## 固定邊界

| 名稱 | Agent Machine 中的意義 | OS 類比 |
|---|---|---|
| Agent | 持久 identity、namespace、memory 與可喚醒 image | executable／process image |
| Goal | specification、postconditions、evidence 與 budget | job contract |
| Round | 一次活躍、可暫停恢復的 goal attempt | logical process |
| Step | 一次 `ask() -> message` 的 stochastic quantum | statement／不可搶占 time slice |
| Tool run | 經 capability 驗證的外部作用 | syscall／I/O job |
| Engine | 有成本、容量、延遲與成功分布的思考資源 | heterogeneous processor |
| Context manifest | 本步實際可見的有序 working set | address space／page table |
| OS worker | 承載 Round 或 tool 的真正 process/container | enforcement vessel |

Agent path 和 OS PID 不可混用。Agent 在兩個 Round 間可以完全 dormant；OS worker 可被回收後
重建。`instance_id`、`round_id`、PID／pidfd 各自標識不同生命週期。

## 一個 machine step

```text
view = compile(state, memory, policy, limits)
proposal ~ engine(preset, view)
decision = validate(proposal, capabilities, budget, world_generation)
next_state = commit(state, decision, evidence)
```

`engine returned`、`agent stopped`、`attempt completed`、`goal achieved` 是四個事件。模型只能
提出 completion claim；verifier 才能依 postconditions 與 evidence 提交 `achieved`。再次呼叫
engine 是 fork 出新 sample，不是 replay；replay 只能讀已保存的 input manifest、原始 response
與 events。

## 分層

```text
operator / agents
       │ typed commands + private namespace
agent_machine                 userspace domain kernel
  ├─ reducer + event journal
  ├─ goal / verifier
  ├─ scheduler + resource ledger
  ├─ context / memory controller
  └─ endpoint / tool dispatch
       │ adapters
Linux                         process、pidfd、cgroup、namespace、LSM、fd
```

`agent_machine` 是唯一協調者，不取代現有真源：

| 既有元件 | 保留責任 |
|---|---|
| `llmkit/llms` | 執行一次 endpoint request；preset 仍只放 connection/model/parameters |
| `agentloop` | 單一 Round 的 Step／tool pairing 與安全邊界 |
| `agent_runtime` | OS worker、sandbox、pidfd/cgroup lifecycle |
| `memory_tools` | object/ref、context manifest 與 working-set compilation |
| `team_tools` | 組織、delegation、task 與父子資源視圖 |
| `agentfs` | Plan 9 式 read/control projection；不是另一份資料庫 |

## Kernel 必守、模型不能自行宣告的事

- 每個 state mutation 都由 typed command 驗證後產生 append-only event。
- stale generation、重複 completion、重複 settlement 一律 fail closed。
- child grant 只能縮小；consumable 先 reserve，不能複製父餘額。
- endpoint/tool capacity 不超賣；lease 到期可回收，晚到結果不能覆寫新狀態。
- source trace 不因 compact 改寫；每個 Step 保存實際 context manifest。
- tool effect 有 operation id；未知結果不能盲目 retry。
- model stop 只結束 attempt，不自動達成 goal。
- projection、prompt 與 path 都不是 authority；enforcement 位於 trusted runtime。

## 非目標

- 不把整套 runtime 寫成 Linux kernel module。
- 不讓 agent path 直接等於 host path、PID、權限或 cgroup path。
- 不把每個 token／AST cell 做成 9P file operation。
- 不承諾任意 goal 在有限 budget 內必然成功。
- 不用「另一個模型也同意」冒充確定性證明。
