# 分期路線與驗收

## Phase 0：跨語言測試架

先建立 fixture runner，不碰 production：

- JSON canonicalization 與 error normalization。
- 同一 vector 同時跑 Python／Janet。
- clean Janet build。
- Linux 為主要 process CI；Windows pure-core 另跑。

Gate：測試能指出哪個 field／transition 不同，而不只回「結果不一樣」。

## Phase 1：`tooljson` vertical slice

實作 parser、strict validator、registry、`exec` argv builder、clip。先不執行 subprocess也可完成多數 pure core。

Fixtures 來自現有 45 關，另補 Windows path、Unicode、binary、timeout、duplicate name、unknown `_type`。

Gate：pure vectors 100% 等價；Linux exec vectors 全過；未知欄位 fail closed。

## Phase 2：共同 value objects

固化：

- `AgentPath`
- permission/resource values
- modelcard validation/lookup
- Round/Step/event ids

Gate：path property tests、subset laws、reserve/refund conservation、card corpus 全過。

## Phase 3：agentloop semantic core

將 Python smoke 劇本輸出成 trace，Janet 實作：pending debt、limits、quiet、stop reason、usage、phase。模型 HTTP 與工具執行都先是假 adapter。

Gate：35 個現有情境等價，再加 completion/enqueue race、cancel、tool-input correlation；不靠真模型。

## Phase 4：LLM 與 tool adapter

把 `langlab-janet/modules/llm-http` 接進 core，先走本機 LiteLLM。補 streaming abstraction，但在 Janet transport 未完成 SSE 前允許 Python adapter。

Gate：fake OpenAI backend、multi-tool、malformed args、truncation、usage、retry 全離線；真 provider 只作非阻塞驗證。

## Phase 5：communication／memory／introspection

依序做：

1. file mailbox schema + fake storage。
2. memory object/address/resolver。
3. Step context manifest。
4. effective-state introspection。

Gate：100 封並發、crash replay、cycle/bytes/depth limit、ACL 不洩漏、同 manifest 可重建。

## Phase 6：runtime/team

先 Janet policy + fake backend，再接 trusted Linux supervisor。team 建在 runtime reserve 和 communication 之上。

Gate：child 不升權、不超賣、失敗只退款一次、task transition 冪等、通知失敗不丟 authoritative state。

## Phase 7：agentfs projection

先 `agent_list`／`agent_read`，再決定 FUSE 或 9P adapter。

Gate：不同 actor 看到不同 namespace；secret 的名稱也不洩漏；snapshot generation 一致；projection read-only。

## 優先順序

```text
tooljson
  → identity + value objects
  → agentloop core
  → LLM adapter
  → communication + memory + introspection
  → runtime + team
  → agentfs mount adapter
```

## 建議的第一個實際 PR

只做三件事：

1. `fixtures/tooljson/*.json`：從 Python smoke 抽出 15–20 個 pure vectors。
2. Janet `tooljson` parser/validator/argv builder，沒有 subprocess。
3. 一個 differential runner，輸出 machine-readable diff。

這個 PR 不碰 agent runtime、不接模型、不做 FUSE，能最小成本證明整條固化方法。

## 何時暫停遷移

- protocol 還在頻繁改版。
- Janet 只能靠大量平台特例追平 Python。
- differential mismatch 無法分類。
- adapter 的安全驗證比 core 更薄弱。
- build/test 無法在乾淨 Linux CI 重現。

遇到這些狀況就讓 Python reference 繼續探索；這不是失敗，而是固化門檻尚未到。
