# 千萬 Agent 的 Assurance 網路

## 真正的大麻煩不是 message routing

千萬個 agent 不只交換資料，也會互相依賴結論：A 的摘要成為 B 的規劃前提，B 的 artifact
被 C 測試，C 的報告又讓 supervisor 判定整個 task 完成。錯誤會沿 dependency graph 擴散，
而且同模型、同 context、同資料源造成的錯誤高度相關。

所以「確定性的流動」不應實作成每個 agent 帶一個 confidence 數字。網路中真正流動的是：

```text
Claim + scope + generation + evidence refs + producer provenance
+ dependencies + unresolved conditions + verifier decision
```

一個 claim 被引用時，不是把 assurance 扣給接收者；而是新 claim 建立 dependency edge。來源
過期、撤回或 scope 改變時，下游 decision 必須精確 invalidation。

## Assurance graph

```text
observation ──> mechanical fact ──> claim ──> artifact
     │                │               │          │
     └─ generation    └─ verifier     └─ deps    └─ consumer claims
                              \          /
                               human node
                         intent / ambiguity / acceptance
```

這延伸既有 [evidence graph](../../design/agent-world/03-memory-context.md)：evidence、mechanical facts、
hypotheses、accepted grounding 與 conclusions 分層。未審 hypothesis 不能因被一千個 agent 引用
就變成真；循環引用也不能自己產生 assurance。

## 大規模下的五種失穩

1. **相關錯誤雪崩**：萬個 child 用同模型／同錯誤資料，同意票數巨大但沒有獨立性。
2. **scope laundering**：只對 `workspace@g7` 成立的測試，被轉述成「此專案永遠正確」。
3. **stale certainty**：上游檔案、policy 或 evidence 已換 generation，下游仍顯示綠燈。
4. **circular endorsement**：A 引 B、B 引 C、C 又引 A，沒有外部 observation。
5. **attention collapse**：所有不確定都丟人審，review queue 爆炸，人開始機械 approve。

因此 assurance network 需要 provenance-aware invalidation、correlation tags、acyclic proof slice
檢查、risk-based sampling 與 human capacity ledger；不能只加更多 agent reviewer。

## 人類介入節點是穩定器

人類節點至少有四種不同功能：

- **意圖錨點**：釐清「究竟想要什麼」、選擇不可由測試推出的 trade-off。
- **歧義裁決**：當多個合理世界都符合形式規格時，選定下一個方向。
- **權威接受**：對限定 scope 承擔 approve/reject 責任，例如發布或不可逆操作。
- **事故斷路器**：偵測 assurance graph 異常、停止 propagation、撤回 grounding 或改 policy。

人類不必也不可能驗每個 leaf，更不是完美真理 oracle。mechanical fact 仍應由工具驗；人的
核心價值是提供外部目標、責任與語境，打破 agent 彼此引用形成的封閉迴路。

## 可擴張的拓撲

```text
leaf agents
  -> local mechanical verifiers
  -> task assurance aggregators
  -> team risk gates
  -> sparse human intervention nodes
  -> organization-level policy / audit
```

aggregator 不把證據壓成一個平均值，而輸出最小 proof slice、缺口、correlation cluster、成本與
推薦 restart。人看到的是少量高影響 cut points：一個決定若會解除大量下游阻塞、影響不可逆
effect，或缺少任何獨立 evidence，就優先進人工 queue。

可採的 human routing 指標：

- downstream blast radius 與 effect irreversibility；
- assurance deficit 類型，是否只有人能解；
- deadline、等待中的 agent 數與 queue age；
- reviewer domain、conflict of interest、近期負荷與疲勞；
- 是否能先縮小 claim、跑測試或建立 counterexample，降低人類閱讀量。

## Interactibility 是網路控制面

若沒有可觀察 snapshot、可取消 wait、typed question、restart palette 與 evidence summary，人類
節點只是塞在 pipeline 中的聊天框。真正可互動的 assurance network 應提供：

```text
request_human {
  claim, decision_needed, alternatives, evidence_slice,
  impact, deadline, fallback, estimated_review_minutes
}

human_decision {
  accept | reject | narrow_scope | request_evidence | delegate,
  authority_scope, rationale, valid_generation
}
```

所有決定進 event journal；新 generation 可使舊 acceptance 過期。人工等待是正式狀態，不能
讓 agent 以「沒有回覆所以大概同意」繼續。

## 核心不變量

- assurance 不因轉寄、複製或投票而守恆增加。
- 每個 accepted claim 都能回到至少一條非循環 evidence／authority slice。
- evidence、decision 與 claim 都綁 scope 和 generation。
- 同源／同模型 reviewer 標示 correlation，不冒充獨立複核。
- 人類 acceptance 只在獲授權範圍內有效，不能越級洗白技術證據。
- reviewer capacity 有 queue、fairness、deadline 與 overload policy。
- 上游撤回能找出受影響下游；不能只改一頁摘要留下綠色 cache。
