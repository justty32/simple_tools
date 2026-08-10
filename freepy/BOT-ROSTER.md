# Cache-aware 的 team member 選擇

日期：2026-08-10；本篇接續 [`BOT-LIFECYCLE.md`](BOT-LIFECYCLE.md) 與 [`BOT-LIFECYCLE-OPERATIONS.md`](BOT-LIFECYCLE-OPERATIONS.md)。

## 結論

team leader 的確應權衡「創造乾淨的新 member」與「重用 cache／history 仍熱的舊 member」，但不是二選一，而是三種操作：

```text
continue old branch   同 member、同 history：連續性與 prefix cache 最佳
fresh branch          同 member、新 working context：保留身分/記憶，減少污染
spawn new member      新身分/權限/責任，可並行且真正隔離
```

**建立 member 應首先表達組織、權限、責任與並行需求，不應只是 prompt-cache hack。**只需要乾淨 context 時，先考慮同 member 的 fresh branch；需要獨立角色、不同 policy、並行或責任歸屬時才 spawn。

## 「cache 省 token」要拆成三筆帳

- **context tokens**：完整 prompt 仍占 context window；cache hit 不會讓長 history 變短。
- **endpoint compute/latency**：prefix hit 可少做 prefill，通常更快。
- **provider billing**：有些 endpoint 對 cached input 折價；費率與計法是 endpoint policy，不能由 `cached_tokens` 猜。

所以 reuse 可能更便宜、更快，卻仍因舊 history 太長而擠掉工作資料、造成 anchoring／污染。反過來，fresh member 雖 cache miss，若 prompt 短很多，總成本仍可能更低。

目前 `agentloop.Limits.tokens` 應繼續按 provider 回報的 logical total 計數，不扣 cached tokens；未來 monetary budget 另按 cached/uncached/output rate settlement。兩者混成一條 token limit 會讓資源守恆失真。

## retention 不能寫死兩小時

[DeepSeek 官方 Context Caching](https://api-docs.deepseek.com/guides/kv_cache/) 說 cache 預設開啟、best-effort，閒置 cache 通常在**幾小時到幾天**內清除，且 response 會回 hit/miss tokens；沒有保證 2 小時。[LM Studio 官方 Idle TTL](https://lmstudio.ai/docs/developer/core/ttl-and-auto-evict) 的 JIT 預設 60 分鐘則是**模型 weights residency**，不是 bot prefix cache。

因此 control plane 只存 `affinity_until + confidence` 的估計，來源是 endpoint capability、最近 hit telemetry 與 latency；不能宣稱某個 bot「兩小時內一定熱」。unknown／evicted／failover 時必須能完整重算。

### 留給未來 cached influence coefficient

先記風險，不在本報告定公式：同一帳號／`user_id`／API key、來源 IP 或 egress route、近期請求量、unique-prefix/token churn、provider 的 per-tenant cache 容量與全站負載，都**可能**縮短實際命中時間。DeepSeek 目前只明說 account concurrency 跨 API key 合併、`user_id` 用於 KVCache／scheduling isolation，未公開 cache 容量或 eviction 公式。

未來算法必須以實測 telemetry 估係數，不把 TTL 當常數；至少保留 `cache_domain`、age、recent requests/tokens/unique prefixes、hit/miss、route 與 confidence。這些只調整 continue/fresh/spawn 的成本分數，不得成為正確性、授權或 freshness 的條件。現在不做 probe、容量模型或自動調參。

## 三種選擇

### 1. continue old branch

適合：同一長任務、未完成 continuation、剛做完 tools 要送 results、舊脈絡本身就是必要輸入。優勢是最長 exact prefix 與最低 handoff；風險是 history 成長、舊假設污染、同 member 無法同 branch 併行。

必須拒絕：history 有壞 pairing／needs inspection、policy/model/tools 已不相容、任務要求獨立判斷、舊 workspace generation 不可安全延續。

### 2. fresh branch on existing member

保留 agent path、incarnation、role memory 與 team 關係，但 working context 從 stable system/tools、task brief、明確 memory refs 重新編譯；舊 trace 不刪除，只是不整包 inline。它可能仍命中共享 system/tool prefix，又避免把全部舊對話帶進來。

這是「想用熟悉成員，但要乾淨 context」的預設答案。branch 必須有自己的 history head/generation；同一 branch 仍只准一個 active Turn。是否允許同 member 多 branch 並行要由 workspace／tool side-effect policy 決定，不能只看 endpoint capacity。

### 3. spawn new member

適合：需要真正不同 identity、permission subset、workspace isolation、獨立責任、平行探索或避免相互 anchoring。新 member 也可能命中與其他 member 相同的 system/tool/template prefix，所以「新成員必然 cache-cold」並不成立。

成本包括 member/grant/allocation/task transaction、leader 管理跨度與新的 artifact ownership；不是建立一個 Python object 的成本。retry 必須以 operation id 保證只生一個 incarnation。

## leader 的決策順序

cache 不能排第一。建議固定成：

1. **hard eligibility**：權限、tools、engine、workspace、deadline、conflict、branch health。
2. **語意需求**：continuity、freshness、independence、parallelism、責任歸屬。
3. **context 品質**：舊 history 的相關度、長度、staleness、待審假設與 compaction 風險。
4. **成本估計**：raw prompt、expected cached/uncached tokens、TTFT、費率、model residency/load cost。
5. **組織限制**：member quota、spawn rate、leader span、task/resource reservation。
6. 在滿足前五項的候選中選擇；cache locality 只是 soft score。

粗略估計可用：

```text
expected_prefill = prompt_tokens - E[cached_tokens]
expected_input_cost = E[cached] * cached_rate
                    + E[uncached] * uncached_rate
total_context_pressure = prompt_tokens              # 不因 cache 減少
```

估值應附 `confidence` 與 p50/p90，而不是一個假精確數字。沒有 telemetry 時按 cold/worst-case reserve；實際 usage 回來再 settlement。

## 給 leader 的配套 tools

不要讓 leader `list_agents` 後把一萬筆塞進 context。加一個有界、deterministic matching layer：

| tool | 回傳 |
|---|---|
| `match_agents(requirements, limit=5)` | 有資格的 top-N member/branch，不讀 private history 正文 |
| `estimate_assignment(candidate, task_ref, mode)` | context/cost/cache/latency range、confidence、risks |
| `assign_task(..., context="continue|fresh", wake=...)` | 對既有 member 建 task＋branch choice |
| `spawn_subordinate(..., context="fresh|fork")` | 新 identity 的既有 saga；fork 不冒充乾淨 context |

candidate 摘要至少含 path＋incarnation、state、role/capability、current task conflict、branch health、raw prompt estimate、last observed cache hit、endpoint/model、affinity confidence、policy incompatibility 與推薦 mode。cache key、其他 agent 私有 prompt、provider internal handle 不暴露給模型。

`match_agents` 先用 hard filters，再用可解釋 score；leader 才做最後選擇。若 policy 標 `freshness=required` 或 `independent=true`，cache 再熱也不能推薦 continue。

## wake 與 roster 的完整交會

leader 選定方案後才建立 durable operation：

```text
continue/fresh existing
 -> assign task -> choose/create branch -> WakeIntent -> scheduler

spawn new
 -> reserve -> derive policy -> create incarnation/member
 -> assign task -> WakeIntent -> scheduler
```

若候選在決策與 commit 間被別的 task 佔用，以 generation CAS 失敗並重新 match，不暗中改派。cache 在這段時間失效不需 rollback；它只是 estimate，不是 correctness precondition。

## 驗收

- 同一 task 比較 continue/fresh/spawn 的完整 prompt、cached/uncached、latency、cost 與結果品質。
- fake endpoint 隨機 expiry/eviction；選擇可能變慢，但不能失敗或送少 context。
- `freshness=required`、權限隔離、workspace conflict 永遠壓過 cache score。
- hot member 不壟斷：公平性與 active conflict 會讓其他 member／spawn 得到工作。
- 同 member fresh branch 不混 history；spawn retry 不重複 member／budget。
- 品質用固定任務集評估，不能只因 token/TTFT 下降就宣告 reuse 較好。
