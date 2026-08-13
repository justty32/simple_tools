# 自我改進與 EvoMap 取捨

Agent Machine 原本就把自我改進列為內建機制。EvoMap 的 GEP（Genome Evolution Protocol）提供了一套
可參考的資料格式與生命週期；我們採用其中能被本機驗證的部分，不把 EvoMap runtime、Hub 或生物學名詞
變成核心依賴。

## EvoMap 實際在做什麼

GEP 目前以三種主要資產記錄一次改進：

| GEP 名稱 | 白話意思 | Agent Machine 對應 |
|---|---|---|
| Gene | 可重用的處理策略，含觸發條件、步驟、限制與驗證 | versioned strategy/policy asset |
| Capsule | 一次實際套用後的結果，含 diff、環境、分數與來源 | improvement candidate + evidence |
| EvolutionEvent | 無論成功失敗都追加的完整稽核事件 | append-only improvement event |

資產以 canonical JSON 的 SHA-256 作 content id，保留 parent、來源與環境資料。完整流程是
Detect → Select → Mutate → Hypothesize → Execute → Evaluate → Solidify：先找訊號和候選策略，明寫預期，
隔離執行，驗證影響範圍，最後才固化成果。外部 Capsule 必須先 staging、依本機環境調整並重新驗證，
不能直接執行。

這些都是好契約。特別是「失敗也留下事件」、「假設先於執行」、「策略和實際結果分開」以及
content-addressed lineage，可直接補強我們 PR 8 的 self-improvement branch。

## 採用的部分

Agent Machine 增加三種自己的 typed record，名稱保持工程用途：

```text
Strategy {
  id, parent_id?, signals, preconditions, steps,
  limits, validation, provenance, content_hash
}

ImprovementCandidate {
  id, strategy_id, base_generation, branch_ref,
  proposed_diff, environment, validation_report,
  measured_cost, measured_outcome, provenance
}

ImprovementEvent {
  id, parent_id?, signal_snapshot, hypothesis,
  candidate_id?, outcome, root_generation_before,
  root_generation_after?, policy_version, evidence_refs
}
```

具體採用：

- append-only 事件、parent lineage、canonical encoding 與 content hash；hash 用於完整性和去重，
  不代表來源可信。
- signal → strategy → hypothesis → attempt → outcome 的因果鏈；失敗與被拒絕的 candidate 也保留。
- 改動前先列檔案／行數範圍、禁止路徑、預算與驗證命令；真正權限仍由 capability policy 決定。
- 外來策略先放 private staging branch，經 schema、hash、來源、license、敏感資料和本機 verifier 檢查。
- token、金額、wall time 與 executor revision 都記在 evidence；排程不用模型自述 confidence 作真相。
- 累積多次成功後可以產生新 Strategy，但它仍是 candidate，要走同一套 eval 與 promotion gate。

## 不直接採用的部分

- 不把 `evolver` Node 程式或遠端 Hub 放進 machine core。Python prototype 與最終 C++ core 都只依賴
  我們凍結的本地 record/schema。
- 不允許網路抓回來的 Gene/Capsule 自動執行、安裝或 publish；remote sync 是後期 optional adapter。
- 不接受 `git reset --hard` 是普遍 rollback。Git 只能回復受管檔案；HTTP、信件、金流與其他外部效果
  必須靠 receipt/reconcile 或補償操作。
- 不把 Git diff 當完整狀態。candidate 必須綁 `base_generation`，promotion 由 machine core 對 active ref
  做 CAS；Git 是可讀 checkpoint。
- 不持久保存原始 prompt、reasoning trace、secret 或完整 tool output 作預設分享內容。分享前只輸出
  allowlisted evidence，明確 redaction，且必須由使用者 opt in。
- 不把品質分數當安全證明。格式通過、測試通過、權限可用、成本合理、效果可歸因是不同 gate。

## PR 8 的最小閉環

```text
signal
  -> 選本地 Strategy
  -> 建立 hypothesis + private branch
  -> executor 在 staging workspace 產生 candidate
  -> deterministic verifier + tests + budget/effect checks
  -> ImprovementEvent(success | failed | rejected)
  -> success 才以 CAS promotion active ref
  -> 另行發布 Git checkpoint
```

初版只支援 repo 內建、人工 review 過的 Strategy；不連 Hub。promotion 只改 versioned ref，不重寫歷史，
也不回滾外部效果。GC 將 active ref、事件 evidence、人工 pin 和未完成 lease 都當 root；被淘汰的
candidate 先 archive，經 grace period 才清除。

## 之後若做 GEP adapter

Adapter 是 importer/exporter，不是 authority。Import 必須：限制大小與深度、strict schema、重算 hash、
驗 license/provenance、移除或隔離 validation command、拒絕 secret/PII，再轉成內部 Strategy。Export 只從
已 promotion 的 revision 生成最小必要資料；原始 context 預設不離開本機。

網路 dispatch 後斷線一律記 `outcome_unknown`，先查遠端 idempotency key／asset id 再決定，不能盲重送。
任何遠端評分、人氣或 credit 只作候選排序訊號，不取得本機執行權。

## 官方來源（2026-08-13 查閱）

- [GEP Protocol](https://evomap.ai/wiki/16-gep-protocol)：資產 schema、七階段流程、content addressing、
  staging、validation、publish gate 與 MCP bridge。
- [EvoMap Evolver](https://github.com/EvoMap/evolver)：官方 runtime repo、資料目錄與使用方式。
- [Evolver SKILL.md](https://github.com/EvoMap/evolver/blob/main/SKILL.md)：實際執行與 rollback 設定。

EvoMap 的協議、runtime、遠端服務與授權條款仍在演進；真正實作 adapter 前，要 pin 明確版本並再次
檢查 schema、license、資料上傳與破壞性 Git 行為，不能依賴本文的盤點時點。
