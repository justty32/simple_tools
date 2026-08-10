# 分期路線與驗收

本章保留 Agent World 各 view 的研究路線；跨 package 的目前實作順序已由較新的
[Agent Machine PLAN](../../freepy/agent_machine/PLAN.md) 與
[FreePy ROADMAP](../../freepy/ROADMAP.md) 取代。

## 原則

先驗證語意，再選 transport；先無損，再做自動 compact；先 read-only，再開 control。`unipath` 已證明 9P/live tree 可行，這裡不需重做概念展示，而要補 production agent 系統缺的 identity、ACL、manifest 與 lifecycle。

## M0：純資料契約

實作：

- `AgentPath`：canonicalize、parent、child、ancestor；保留 `.agent`。
- `NodeMeta`：declared/effective/observed 分層與九軸 schema。
- `MemoryTarget`、source span、content hash、ACL。
- Step/Round/event/stop reason records。

Gate：segment prefix 不誤判；未知欄位 fail closed；所有 object 可 serialize；metadata 不把 requested 當 effective。

## M1：read-only agentfs vertical slice

實作 provider、per-actor view、`list/stat/read` 與 model tools：

```text
agent_list(path)
agent_stat(path)
agent_read(path, max_bytes)
```

先投影 self identity、state、Round/Step、public env、resources、effective permissions 與 child presence。Windows 直接跑 Python resolver，不依賴 mount。

Gate：self/ancestor/peer/operator 四種 view；secret/name 不洩漏；stale instance；generation snapshot；bytes/list limit；provider error fail closed。

## M2：不可變 trace、objects 與 Step manifest

先把現有大型 tool result unload/load 接到 content-addressed store。每次 `ask()` 保存實際 blocks manifest，不改 source trace。

接著允許人工 promote assistant 的 research/report span；當前 user instruction 和未閉合 tool pair 仍 pinned inline。

Gate：相同 manifest 可重建相同 model input；tool pairing 完整；source span hash 可驗；ACL 作用於 list/search/load；link depth/bytes 有界。

## M3：organized memory graph

加入 catalog、tags、backlinks、versioned aliases，以及：

```text
evidence -> mechanical facts
         -> hypothesis -> review/certificate -> accepted grounding
accepted facts + rules -> derived conclusions
```

結論保存 dependency set；grounding 撤回觸發精確 invalidation。先用規則與全文搜尋，不急著 embedding。

Gate：未審 hypothesis 不進確定性推論；撤回後沒有 stale conclusion；一個 object 可出現在多個 view；alias 更新不破舊 ref。

## M4：deterministic context compiler

只用 pin、recency、task links、dependency 與 token estimate，產生 full/section/summary/card/cold LOD。加入 hysteresis 與每 block 選擇理由。

Gate：current instructions 永遠 inline；預算硬上限；不發生 load/unload thrash；compiler 本身可重播。LLM ranking 延後到有 eval 與 `nondeterministic` 證書後。

## M5：team、tick 與 read-only cross-agent memory

每個 agent 保有獨立 Round；team task 可建立 tick/barrier，child 用 report/artifact refs 交付，不共享私有 prompt。parent budget 先 reserve 再分配。

Gate：同儕與跨 subtree 越權被拒；取消能喚醒 blocked tool；父停止的 propagation 可預測；資源守恆；tick snapshot 可指出每個 child 的 stop reason。

## M6：typed control protocols

先加入 self `ctl` 的 pause/resume/stop，再加入 tool `clone` 與 spawn transaction。write 一律轉 typed supervisor operation。

Gate：重複 commit/stop 冪等；grant revocation 生效；spawn 失敗補償與退款一次；任何 projection field 都不能繞過 state machine 直改。

## M7：transport 與 sandbox backend

provider 穩定後才接：

- 9P：最符合 per-session namespace，且已有 unipath 實證。
- FUSE：本機工具方便，但 container `/dev/fuse` 與平台成本較高。
- snapshot export：診斷／測試，只讀、不可回寫。

runtime 再選 rootless Podman/container 或其他 OS sandbox；agentfs 與 sandbox 分開驗收。

Gate：mount adapter 與 model tools 看見相同語意；無 mount 權限仍可用 resolver；sandbox evidence 顯示實際 cgroup/network/mount policy，不靠宣告。

## 第一個可做切片

```text
AgentPath
  -> fake runtime/team providers
  -> per-actor agent_list/read
  -> immutable Step manifest
  -> tool-result memory ref
```

這一刀完全離線、無容器、無模型也能測，卻同時驗證 identity、namespace、ACL、generation 和 context/memory 的共同 address model。

## 尚待選擇

1. `.agent` metadata tree 與每-agent `/self` view 的最終 mapping。
2. accepted grounding 的簽署者與撤回 policy。
3. tick 是否進核心，或先維持 team layer 的可選 orchestration。
4. memory stable alias 的命名與跨 task retention。
5. 9P authentication/session 與 supervisor identity binding。
6. 哪些九軸欄位直接沿用 ai_core，哪些只做 adapter 對接。

這些都不阻塞 M0–M2；先跑出真資料再定，會比提前發明完整 Lisp/9P 世界更可靠。
