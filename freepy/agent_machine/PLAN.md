# Agent Machine 實作計畫

## 目標與策略

先建立可離線驗證的 userspace domain kernel，再接真 endpoint、Linux sandbox 與 9P/FUSE。
第一版優先證明資源守恆、crash recovery、公平排程與「停止不等於 goal achieved」；不追求
分散式部署或 kernel module。

## 預定 package

```text
agent_machine/
  records.py       ids、enums、immutable records、schema versions
  commands.py      typed requested mutations
  events.py        accepted facts；append-only
  reducer.py       state + event -> state
  store.py         transaction、generation CAS、snapshot/replay
  resources.py     pool、reservation、lease、charge、settlement
  scheduler.py     admission、fair queue、pressure、backoff
  goals.py         claim、evidence、verifier decision、restart policy
  kernel.py        command validation + event production
  adapters.py      endpoint、Round runner、tool/runtime ports
```

核心不得 import OpenAI SDK、FUSE、Podman 或特定資料庫。clock、id generator、store、endpoint、
tool executor 都可注入 fake；所有 offline gates 不用網路。

## 最小 public operations

```text
register_bot(image) -> bot_id
submit_goal(bot_id, goal, resources, operation_id) -> goal_id
enqueue_round(goal_id, context_ref, operation_id) -> round_id
tick(max_dispatch) -> [Dispatch]
accept_round_result(lease_id, raw_result, usage, operation_id)
accept_tool_result(tool_run_id, result, effect_status, operation_id)
submit_claim(round_id, evidence_refs, operation_id)
verify(goal_id, verifier, operation_id) -> Decision
control(round_id, pause|resume|cancel|restart, operation_id)
snapshot(actor, object_id, generation=None)
```

實作可用 Python methods；這是 logical ABI，不先綁 HTTP、CLI 或檔案協定。每個 mutation 回傳
新 generation 及產生的 event ids。

## M0：純資料模型與 reducer

建立 ids、records、commands、events、state machines 與 generation CAS。用 in-memory event store
重播 snapshot；未知 schema/event fail closed。

Gate：同一 event stream 得到相同 state；duplicate operation 不重複 mutation；stale generation
被拒；model stop 只能產生 candidate/attempt stop，不能直接寫 achieved。

## M1：資源 ledger 與 fake scheduler

實作五類 resource、父子 pool、reserve/lease/charge/settle，以及 fake clock/endpoint。排程先用
priority class + owner/team weighted round-robin + aging；endpoint pool 另有 concurrency 與 token
bucket。估算成本 reservation，完成後依 actual usage 結算。

Gate：並發 property tests 不超賣、不雙重退款；lease expiry 後 late result 無效；單一 owner 不能
餓死其他 owner；429 backoff 不形成 herd；10,000 dormant bots、數百 queued Rounds、固定 K 個
Step slots 的離線模擬保持有界 active state。

## M2：將 agentloop 抽成可逐步驅動的 Round machine

先完成 `agentloop/PLAN.md` 的 lock、safe boundary 與 settlement 修正，再抽出一次只前進到下一
safe boundary 的 `advance()`／runner protocol；現有 `run()` 保留為反覆呼叫 runner 的便利 API。

Agent Machine adapter 把 `AskStep`、`RunToolBatch`、`WaitInput`、`Candidate` 變成 durable commands。
不讓 scheduler 直接修改 `LLM.history`。

Gate：既有 35+ 離線案例維持；crash 在 dispatch 前／送出後／tool effect 後都能分辨；未閉合
tool pairing 不重做；pause/cancel 只在已定安全邊界生效。

## M3：durable store 與恢復

先用 SQLite WAL 或等價單機 transaction store；event + ledger mutation + outbox 同 transaction。
snapshot 只是加速，隨時可由 events 重建。worker 使用 inbox/outbox lease，不直接擁有真源。

Gate：在每個 transition 注入 crash 後重啟，queued work 不丟、committed effect 不重做、未知
effect 進 reconciliation；舊 instance／lease completion 不能污染新 generation。

## M4：Goal、evidence、condition/restart

加入 mechanical verifier port、completion claim、evidence provenance 與 explicit decisions。先支援
`retry`、`repair`、`switch_engine`、`shrink_context`、`escalate`、`abort` 六種 typed restart；
policy 列出當前允許集合，模型只能建議。

Gate：假模型「聲稱完成但檔案未改」不會 achieved；mechanical evidence 過期後 decision 失效；
probabilistic reviewer 不被標成 proof；未知 tool outcome 不能走普通 retry。

## M5：Linux enforcement adapter

以獨立 tool executor process 為第一個實體邊界：pidfd lifecycle、delegated cgroup v2、最小 mount/
network namespace、seccomp/Landlock policy。logical resource pool 與 cgroup usage 分開記帳並用同一
operation id 關聯。先不做 `.ko`；out-of-tree module 只有在量測出 userspace 無法守住的 invariant
後另立 proposal。

Gate：process escape/visibility 測試、CPU/RAM/PID hard ceiling、stop/reap 無 PID reuse race、
parent cancellation 可回收 children；無可用 sandbox 時 effective state 不得聲稱已隔離。

## M6：Plan 9 namespace projection

共用 `agentfs` provider contract，投影 `/self`、`/goals`、`/rounds`、`/resources`、`/conditions`、
`/restarts`、`/events`。先 `list/read/stat`；`ctl` 只翻譯成上述 typed operations。之後才選 FUSE
或 9P transport，並實作 `flush/clunk` 對 cancellation/lease release 的明確語意。

Gate：per-actor view、generation snapshot、revocation、bytes/list limit、blocked read cancellation；
任何檔案 write 都不能繞過 kernel validator 或直接改 projection。

## M7：Context pager、team 與 scale policy

接 `memory_tools` manifest/compiler；再讓 `team_tools` 的組織樹成為 resource pool hierarchy。
加入 pressure 指標、context thrashing protection、child reserve、goal delegation 與 task review。

Gate：current instruction/tool pairing 永遠 pinned；memory ref ACL 與 load budget成立；父子資源
守恆；deadlock/orphan/zombie 掃描；scale simulation 的 queue age 與 active memory 有界。

## 第一個提交切片

只做 M0 + M1 的最窄 vertical slice：`records/events/reducer/resources/scheduler`、in-memory store、
fake clock、fake stochastic endpoint 和 deterministic tests。先不要 import `agentloop`；用 scripted
responses 模擬 success、false completion、429、timeout、late result 與 verification failure。

完成定義：能從 event log 展示兩個 owners 競爭一個 endpoint slot，失敗 attempt 被 verifier
退回、resource 正確結算、另一 owner 不餓死，最後只有帶有效 evidence 的 claim 進 `achieved`。

## 跨文件決定

- 本文件是 Agent Machine 的實作順序；[`../ROADMAP.md`](../ROADMAP.md) 仍負責全 repo 依賴。
- Round／Step 術語仍以 [`../agentloop/ROUNDS.md`](../agentloop/ROUNDS.md) 為準。
- agentfs、memory、runtime、team 的局部不變量仍由各自 `PLAN.md` 定義；衝突時以這份較新的
  上層邊界為準，局部細節不得破壞上層資源守恆與 commit 語意。
