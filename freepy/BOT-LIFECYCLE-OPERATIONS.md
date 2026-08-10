# Bot lifecycle：tools、cache 與恢復

日期：2026-08-10；狀態與完整 wake/sleep 流程見 [`BOT-LIFECYCLE.md`](BOT-LIFECYCLE.md)。

## 配套 tools

模型可見工具應是 typed control request，不是任意改 snapshot state：

| tool | 語意 |
|---|---|
| `agent_status(target)` | state、Turn、pending causes、checkpoint、blocked/fault reason |
| `list_agents(scope, state, cursor, limit)` | 有界列出授權 subtree；不提供全域 dump |
| `assign_task(..., wake="run|standby|no")` | task＋budget＋通知＋可選 wake 的高階 saga |
| `wake_agent(target, reason, task_id=None)` | 喚醒既有 bot／continuation；立即回 operation id |
| `sleep_agent(target, reason, after="safe")` | 主管要求 descendant draining；冪等 |
| `set_idle_policy(target, mode, trigger_preset)` | dormant/standby＋有限 preset，不收任意 event expression |
| `yield_self(mode, reason, report=None)` | 自己在安全邊界後 standby/dormant |
| `schedule_wake(target, at, reason)` | 建 durable alarm，不占 worker |

既有 `send_message`、`report_task`、`collect_agent` 保持原義：send 不暗中 wake，collect 不等於 sleep。operator-only 的 `quarantine/repair/retire/discard_continuation/force_wake` 不給模型。

內部 mutation 回 `{operation_id, accepted, effective_state}`，不阻塞等完成；模型 tool adapter 再序列化成既有契約要求的字串。查詢可重做，mutation 以 operation id 冪等。path ancestry 不是授權，仍需 self／direct-child／manage_subtree scope 與正確 incarnation。

## endpoint cache 有三種，不要混在一起

```text
model residency   模型 weights 是否還在 runner 的 RAM/GPU；key 約為 endpoint+model
prefix/KV cache   某段 prompt token prefix 的 KV；key 約為 endpoint+model+prefix
local input cache system/tools/history manifest 的解析、序列化或 token estimate
```

前兩者是 endpoint 資源，可能有 TTL、slot、LRU、tenant scope，且常常不可觀察；最後一種由 freepy 可靠控制。HTTP connection pool 另屬 transport，不應拿來代表 prompt cache。

目前 repo 已有可用的觀測地基：`llms` 把 provider usage 正規化成 `usage["cached"]`；
proxy 的 capability 只能表示「呼叫端能否觀察」，不能反證 runner 內部完全沒有 KV
reuse。scheduler 必須接受 `true | false | unknown`，preset 不承載 capability。

## cache 不屬於 bot 正確狀態

dormant bot 每次 wake 都必須從磁碟 record 重建**完整 request**。不得只保存 provider cache handle、假設同一 worker 還活著，或因 cache miss 改變 history。cache eviction、endpoint restart、replica failover 時，結果仍應語意正確；cache 只改 latency／費用。

因此 bot lifecycle 不增加 `hot` 狀態，而另存可失效 hint：

```text
CacheHint {
  endpoint, model, mode,
  stable_prefix_hash, history_head,
  observed_at, cached_tokens, prompt_tokens, latency,
  affinity_until?, confidence
}
```

hash 必須來自實際 request 的 canonical prefix：model/mode、system blocks、tools schema（含順序）與 immutable history prefix。時間、cwd、wake reason、task/message 等易變資料放在後段；不要在 system 開頭塞每次不同的 timestamp。相同角色模板可共享 content-addressed prefix，但不能為命中率混用 bot identity、私有 history 或 ACL scope。

## cache-aware scheduling

cache 進一步如何影響 leader 選擇舊 member、乾淨 branch 或新 member，見 [`BOT-ROSTER.md`](BOT-ROSTER.md)。

排程順序仍以授權、deadline、priority、fairness 為主，cache locality 只能當有界 tie-break：

1. policy 先選合法 endpoint/model；不可為 cache hit 偷換較弱或未授權模型。
2. 同能力 replicas 間，以 stable prefix/bot hash 做 consistent affinity；failover 仍送完整 request。
3. Round 回 tool calls 後，保留短期 `affinity_until`。工具結果回來時，在不破壞公平性的前提下儘快排下一 Round，利用仍熱的 prefix。
4. 多個相同 model 的工作可小批分組，降低 local runner weights 換入換出；不能讓冷 model 永久 starvation。
5. hit、miss、unknown、cached/prompt tokens、TTFT/latency 按 endpoint/model/prefix class 記 telemetry，以實測估 TTL/hit probability；不要硬編 provider TTL。

不要為保 cache 主動送空 prompt／keepalive Round；那會花 token、製造 history／隱私問題。要保本機 model weights 是 endpoint operator policy，不是喚醒 bot，也不給 leader 一支 `pin_cache` tool。sleep/dormant 的明確要求永遠勝過 cache locality。

cache 會使 standby 有實際效益，但只是 scheduler hint：一個剛完成 Round、很快會收到 tool results 的 bot，短暫 standby 並保留 affinity 比立即和其他一萬個 bot round-robin 更省 prefill；沒有 pending work 時仍不應因 cache 而自行醒來。

## history 與 cache 的取捨

append-only history 天然保留前綴；compact、重排 tools、改 system 或重新序列化都可能使後續 cache miss。context compiler 應保存 immutable source trace，產生 deterministic manifest，並記錄這次為何切 prefix；不能為了 cache 保留超出 context／ACL／token budget 的資料。

若 provider 對 cached tokens 有不同計價，scheduler 可把預估費用納入 cost，但 `cached=None` 不是 0，cache hit 也不等於免費。budget 先按 worst-case prompt reserve，拿到 usage 後再依實際 provider accounting settle，避免靠猜測超賣。

## 失敗與恢復

- supervisor crash：從 event＋snapshot 重建 queue；過期 activation lease 回收，舊 activation commit 被 fencing token 擋。
- endpoint/cache restart：hint 失效即可；完整 request 可在任一合法 replica 重算。
- endpoint error：按 endpoint policy backoff；Round attempt／token cost 留紀錄，只提交一個 response。
- tool worker crash：可能有副作用，標 `needs_inspection`，不自動重跑。
- wake storm：同 bot causes 合併；per-team quota、cooldown、causal depth 與 fairness 擋互相叫醒。
- dormant 收信不醒；standby 只被符合 policy 的事件喚醒；task/message/report 不因 sleep 消失。

## 離線驗收

用 reducer＋fake scheduler 建一萬個 bot records、固定 K 個 Round slots，至少驗：

- duplicate wake 只生一個 activation；task/message 同時到達可合併。
- 越權 wake/sleep 被拒；leader standby 可被 child terminal 喚醒。
- sleep during Round/tool 到安全 checkpoint；stale activation 不可提交。
- crash recovery 沒有幽靈 active；不確定 tool side effect 不重跑。
- fake cache 的 hit/miss/expiry/unknown 都不改輸入與結果；replica failover 仍成功。
- hot affinity 確實降低 prefill，但在硬上限內，cold bot 最終一定被服務。
- prefix canonicalization 穩定；system/tool/history 任一實際變更都產生新 hash。
