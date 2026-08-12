# Assurance、成本與可互動性

## Agent Machine 管的不是一種資源

使用「確定性、金錢、可互動性」描述 agent OS 很有力量，但三者的數學性質不同：

| 抽象量 | 正確類型 | Machine 可以保證什麼 |
|---|---|---|
| 美元、token、Step、tool allowance | consumable | reserve、charge、settle，不超賣或雙重退款 |
| endpoint slot、reviewer 名額 | capacity／rate | lease、公平 queue、deadline、逾期回收 |
| workspace writer、secret route | exclusive capability | 單一 owner、scope、revocation |
| 「結果有多確定」 | assurance／risk claim | 保存 evidence、scope、世代、方法與失效條件 |
| 可互動性 | service contract | backend 是否真的能 pause、input、explain、restart |
| 人類注意力 | consumable-like capacity | queue、估計分鐘、疲勞、優先序與實際用量 |

現有 [資源分類](../../freepy/future/agent-machine/RESOURCES.md) 已說 success probability 不是守恆餘額。
因此「確定性」較適合改稱 **assurance**：有證據支持一個限定範圍命題的程度，而非能裝進帳戶
轉帳的物質。

## Assurance case，不是一個信心分數

```text
AssuranceCase {
  claim, goal, scope_ref, world_generation,
  evidence_refs, verifier_kind, independence_tags,
  unresolved, status, valid_until, invalidated_by
}
```

三種 verifier 各自回答不同問題：

- mechanical：test、schema、hash、runtime observation；只對特定 scope/generation 成立。
- probabilistic：critic、judge、歷史校準；仍有 model/provider/prompt provenance。
- authoritative：使用者或獲授權 supervisor 接受；代表責任與意圖的裁決，不等於自然真理。

同模型多次同意通常高度相關，不能套獨立 Bernoulli 公式。真正增加 assurance 要改變 evidence
來源、engine、解法、測試或 verifier；不同證據也不能不分量綱地加成一個 97 分。

## 可互動性要拆成 contract 與成本

```text
InteractionContract {
  mode, pause_resume, cancel, input_schema, deadline,
  explanation_level, restart_set, fallback
}

HumanAttentionLease {
  reviewer_pool, task_scope, priority, deadline,
  estimated_minutes, actual_minutes, correlation_id
}
```

「有聊天框」不等於可互動。真正有用的是：人能看見哪個 generation、目前卡在哪個
condition、有哪些合法 restart、介入會在何 safe boundary 生效，以及不回覆時採何 fallback。
可理解性也不能只用文字長短衡量；較實際的 proxy 是 review time、澄清次數、人工推翻率、
recovery 成功率與 notification abandonment。

## 三者的交換不是線性的

多花 token 可以買到另一個 proposal 或更多 evidence，但不保證正確；增加測試和 snapshot 可能
同時增加成本與人類可理解性；每一步都要求 approve 反而會造成疲勞，降低真正 assurance。
因此 scheduler 應找 Pareto frontier，而非把三者壓成一個總分。

| 方案 | 成本 | assurance | 人類互動 |
|---|---:|---|---|
| A：單次模型 patch | 低 | 只有 proposal | 無 |
| B：patch + deterministic tests | 中 | mechanical evidence | 可讀 status/diff |
| C：B + 異質 critic | 較高 | mechanical + probabilistic | 無額外人工 |
| D：C + 人類 review | token 中、人力高 | 加 authoritative acceptance | 一次高資訊 gate |
| E：多個同模型投票 | 較高 | 高相關 evidence，收益可疑 | 無 |

結果不應寫成 `D certainty=0.99`，而應說：B 是最低成本的 mechanically verified 解；C 在不
消耗人力下增加異質證據；D 是某高風險 policy 唯一允許提交的解。

## Assurance deficit 是 condition

```text
condition: assurance-insufficient
evidence: [tests-missing, observation-stale, reviewers-correlated]
restarts:
  run-mechanical-check
  switch-engine
  request-human-review
  narrow-claim-scope
  defer-unverified
  abort
```

policy 先守 hard constraints：ACL、金額上限、deadline、required verifier、不可逆 effect gate；
再按預期風險下降／美元、token、時間與人類分鐘排序。模型只能建議 restart，不能因自己很有
信心便放寬要求。

## 這會改變資源排程的問題

傳統 scheduler 問「誰拿 CPU」；這台 machine 問：

> 對這個 claim，下一塊錢、下一個 Step、下一次測試或下一分鐘人類注意力，放在哪裡能最大幅
> 降低仍相關的風險，同時不破壞 hard constraints 與公平性？

這不是保證有一個完美效用函數。第一版可只保存 typed evidence、成本與 interaction receipt，
以規則挑選 restart；等累積真實校準資料後，再估 marginal risk reduction。
