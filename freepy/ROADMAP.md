# FreePy 實作路線

這份只排跨 package 的依賴與驗收順序；各層資料模型和關卡見自己的 `PLAN.md`。所有新規劃檔
維持 8 KB 以下。

目前的上層執行與資源模型以 [Agent Machine](agent_machine/README.md) 為準；Plan 9、Lisp、path、
memory 與多 agent 世界的廣泛設計背景見 [Agent World](../docs/agent-world/README.md)。

## 依賴圖

```text
                         agent_machine core
                 records / events / resources / goals
                                  │ ports
            ┌───────────┬─────────┼──────────┬───────────┐
            ▼           ▼         ▼          ▼           ▼
       agentloop    llmkit     runtime     memory      team/comm
       Round runner  endpoint   OS worker   context     organization
            │                      │           │           │
            └──────────────────────┴───────────┴───────────┘
                                   │
                         introspection / agentfs
```

`agent_machine` 是 userspace domain kernel：它協調各層但不吸收 provider、sandbox、memory store
或 organization 的實作。核心只依賴 ports 與純資料 schema。

`agent_identity.py` 仍是 communication、runtime、team、agentfs 共用的 logical path parser；
agent path 不等於 OS PID、host path、authority 或 cgroup path。

## Milestone 0：Agent Machine 純核心

完成 [`agent_machine/PLAN.md`](agent_machine/PLAN.md) 的 M0 + M1：records、commands、events、
reducer、resource ledger、fake clock 與 fake stochastic endpoint。此階段不 import `agentloop`，
也不用模型、Podman、FUSE 或網路。

Gate：event replay 決定性；generation CAS；operation 冪等；父子資源不超賣；lease late result 無效；
公平 queue 不飢餓；false completion 不會變 goal achieved；10,000 dormant bots 的 active state 有界。

## Milestone 1：Round control 與逐步 runner

先完成 `agentloop/PLAN.md` 的 lock、completion/enqueue、安全邊界、settlement 與 exception 修正；再
抽一次只前進到下一 safe boundary 的 runner protocol，保留既有 `run()` 作 convenience loop。

Gate：現有離線案例全過；追加指令不丟；pause/stop 不多放行 Step；tool pairing 不重做；runner
能產生 `AskStep`、`RunToolBatch`、`WaitInput`、`Candidate` 等 durable commands。

## Milestone 2：durable control plane

用 transaction store 保存 event、ledger、outbox 與 snapshots；接 Agent Machine scheduler 與 Round
runner。Bot record 可 dormant，queued Round 不需一 Round 一 task，只有取得 lease 的 Step 進 runtime。

Gate：每個 transition crash injection 後可恢復；queued work 不丟；effect unknown 可辨識；舊
instance/lease 不污染新 generation；endpoint 恢復時沒有 retry herd。

## Milestone 3：Goal、evidence 與 restart

加入 completion claim、mechanical verifier、probabilistic evidence 與 user authority。採 Lisp
condition/restart 形狀，但 restart 是 typed policy operation，模型只能建議。

Gate：模型自稱完成不能過關；evidence 過期使 decision 失效；retry/repair/switch-engine/escalate
都有 budget 與 audit；未知副作用不能普通重試。

## Milestone 4：identity、communication 與 runtime enforcement

1. `agent_identity.py`：canonical path、parent、ancestor、reserved `.agent`。
2. communication vertical slice：兩個 identity 的 durable mailbox。
3. `agent_runtime` fake backend：policy derivation、budget reserve/refund、instance lifecycle。
4. Linux adapter：獨立 tool executor、pidfd、delegated cgroup、namespace、seccomp/Landlock；再評估
   rootless Podman。

Gate：path prefix 陷阱、sender 偽造、child 升權、資源超賣、PID reuse、cleanup 與 sandbox
effective-state 全有離線／整合證據。沒有 sandbox 時不得聲稱已隔離。

## Milestone 5：memory 與 context pager

memory 先做 address/store 與 immutable source span，再接每步 context manifest。依 Agent Machine
resource controller 做 pinned/working/linked/cold、load budget、hysteresis 與 pressure eviction。

Gate：JSON `$ref`、Markdown link/heading、ACL、cycle/depth/bytes/token 上限；source trace 不變；
current instruction 與未閉合 tool pairing 永遠 pinned；同一 Step manifest 可重建。

## Milestone 6：team resource hierarchy

依序完成 member/grant、allocation、task/report、communication notification，最後才做
`spawn_subordinate` saga。organization path 對應 cognitive resource pool hierarchy；OS cgroup 只
對應實體 worker，不冒充 token controller。

Gate：權限單調縮減、資源守恆、task transition 冪等；通知失敗不丟 task；父子 cancellation、
orphan/zombie、deadlock wait graph 有明確處理。

## Milestone 7：Plan 9 projection

先以 `agent_list/read/stat` 驗 per-actor namespace、provider、generation 與 capability；投影 Agent
Machine 的 goals、rounds、resources、conditions、restarts、events。`ctl` 只能翻譯成既有 typed
operation。需求證明後再選 FUSE 或 9P adapter。

Gate：self/ancestor/peer view、revocation、secret、snapshot、bytes/list limit、flush/clunk；任何
projection write 都不能直接 mutation 真源。

## 第一個可動手的切片

目前下一個提交是 Agent Machine M0 + M1 的最窄 vertical slice：

```text
records + events + reducer
resource pools + reservation/lease/settlement
fair scheduler + fake clock + fake stochastic endpoint
```

完整完成定義見 [`agent_machine/PLAN.md`](agent_machine/PLAN.md#第一個提交切片)。這一步刻意不接
真模型或現有 `agentloop`：先用 scripted results 證明資源守恆、late result、false completion、
verification failure 與公平性，再進下一個 milestone。
