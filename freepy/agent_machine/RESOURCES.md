# 資源管理

這份提煉 OS 長年累積的機制，不照搬硬體名詞。每種資源先分類，再選 controller；不能把所有
東西都塞成一個 `limit` 數字。

## 五類資源

| 類型 | Agent Machine 例子 | 正確機制 |
|---|---|---|
| consumable | token、金額、Step、tool-call allowance | reserve、charge、settle；不可複製 |
| capacity | endpoint slot、tool worker、CPU/RAM/PID | lease／semaphore、hard ceiling |
| rate | RPM、TPM、外部 API QPS | token bucket／deadline-aware queue |
| exclusive capability | workspace writer、device、secret route | owner handle、scope、revocation |
| reclaimable cache | prefix/KV cache、compiled context、索引 | affinity、pressure、eviction；不可影響正確性 |

Context 同時有 per-Step hard capacity（context limit）和可 reclaim working set；memory object store
則是 backing store。Goal success probability 是排程資訊，不是可守恆的餘額。

## Controller 的共同形狀

每個 pool 至少有：

```text
identity + parent
effective capacity / rate / budget
reserved + used + reclaimable
weight / priority / deadline
pressure + events
generation
```

操作固定為：

```text
request -> admission -> reserve -> lease -> charge -> settle -> reclaim
```

- admission 在 dispatch 前證明目前可接受工作；排隊不等於已保留所有資源。
- reservation 從 parent 可用量扣除，child 只拿到切出的份額。
- lease 有 owner、attempt id、期限與 generation；late completion 不得套到新 lease。
- charge 依實際使用追加；不可因 response 無效或 goal 失敗而抹掉已發生成本。
- settlement 只執行一次，退還未用 consumable 並釋放 capacity。

所有 operation 帶 idempotency key，ledger transition 與 event append 必須在同一 transaction；否則
crash 會產生幽靈額度或雙重退款。

## 從 cgroup v2 借的規則

- **階層控制**：parent 控制 immediate children 的分配；child 只能在 effective ceiling 下再分。
- **weight 與 max 分開**：weight 表示競爭時的比例，max 是不可突破的上限。
- **protection／soft pressure／hard limit 分開**：可映射成 `reserve`、`high`、`max`，不要只有一刀切。
- **accounting 先於 policy**：`current`、`used`、`throttled`、events 可觀察後，policy 才能調整。
- **single writer／delegation**：每個 pool 只有一個權威 controller；下放 subtree 要明確交界。
- **no internal process 類原則**：組織節點負責分配時，實際工作盡量落在 leaf，避免 parent 自己消耗
  一份無法與 children 公平比較的隱藏資源。

OS cgroup 只管真 process 的 CPU/RAM/PID/I/O。Agent Machine 用同樣形狀另建 cognitive cgroup；
兩棵樹可以對應，但 token charge 不可假裝成 Linux cgroup controller。

## Endpoint scheduler v1

每個 endpoint/model pool 維護 concurrency、RPM/TPM buckets、健康／backoff、estimated cost 與
cache affinity。排程單位是 Step，不是整個 Round。

1. 只將 context、policy、budget 都準備完成的 Round 放進 `runnable`。
2. 先按 priority class；同級以 team/owner 做 weighted fair queue，等待時間提供 aging。
3. admission 同時取得 endpoint slot、rate permit 與最大成本 reservation。
4. Step 發出後通常 cooperative 跑完；cancel 是 best effort，不假裝可回收已花成本。
5. 回傳後 charge actual usage、更新 latency/error pressure，再釋放 slot。
6. 429/5xx 進 pool-level backoff + jitter，避免每個 Round 各自形成 retry herd。

不得把長 prompt 的工作永久餓死；估算誤差以 actual charge 修正 deficit。deadline 不是無限 priority，
超出 admission 能力時要拒絕／降級／通知，而不是讓全系統失去公平性。

## Pressure、overcommit 與降級

只看 utilization 不足以知道資源短缺是否真的傷害工作。仿 PSI 記錄各 pool 的：

- `some_stalled`：至少一個 runnable Round 因此資源等待。
- `full_stalled`：pool 有需求但沒有任何 Round 能推進。
- wait histogram、queue age、throttle time、deadline miss、retry amplification。

pressure 越界時依序採：停止 admission、evict cache、縮小非 pinned context、降低 speculative
samples、遷移 endpoint、暫停低優先工作，最後才選 victim。hard exhaustion 必須留下正式 reason
與可恢復 checkpoint，不能讓 queue 靜默消失。

## Context controller

```text
source trace / objects    backing store
manifest                  mapping + exact order
inline blocks             resident set
memory ref                checked handle
compiler                  pager
```

policy／當前指令／未閉合 tool pairing 是 pinned；working pages 依 task、recency、explicit load 與
dependency 選入；其餘降成 card 或 cold。eviction 不改 source；load 有 depth、bytes、token、ACL
與 hysteresis，避免 link bomb 和反覆 page thrashing。

## OS worker 與 logical resources

真正 tool/agent worker 才交給 Linux：pidfd 管無 PID-reuse race 的 lifecycle；cgroup v2 管
CPU/RAM/PID/I/O；namespace、seccomp、Landlock 管可接觸的 OS surface；PSI 回報實體 pressure。

若多個 logical agents 共用一個 Python process，Linux 無法逐 agent enforcement。v1 至少讓有
副作用的 tool executor 獨立成 process；是否一個 active Round 一個 worker，以量測後決定。

## 必守不變量

- `sum(child reservations) + parent used <= parent effective consumable`。
- active exclusive lease 對同一 resource/object 最多一個 owner。
- capacity release、refund、effect commit 都 exactly-once at ledger level。
- revoked/stale handle 不能因名稱重新解析而指到新 instance。
- pressure policy 可以降低服務品質，不能降低 ACL 或 source retention。
- cache miss 只影響性能；任何 correctness 依賴 cache 都是設計錯誤。
