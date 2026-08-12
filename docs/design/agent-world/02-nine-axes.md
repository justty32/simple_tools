# 九軸作為契約與推導系統

## 先釐清「九軸」的兩種說法

`ai_core` 的概念框架列九個正交問題：I/O、生命週期、跨呼叫狀態、資源、可中斷性、執行保證、組合、環境、確定性／隨機性。

現行 library spec 則有九個主要 metadata fields：

```text
entries, lifecycle, state, state_dirs, resources,
interruptible, guarantee, dry_run, nondeterministic
```

兩者不是一一對應：`state_dirs`、`dry_run` 是既有軸的細化；組合與環境刻意由 caller/runtime 管，不放進 leaf 自述。設計 agentfs 時應保留這個區分，不要為了湊九個欄位創造假對稱。

## 套到 agent 世界

| 概念軸 | 要回答的問題 | agent/tool 範例 |
|---|---|---|
| I/O | 從哪收、往哪送、batch/stream/interactive？ | inbox、tool result、user input request |
| lifecycle | 自行結束還是持續等待？ | one-shot worker、persistent supervisor |
| state | 是否跨呼叫保留外部狀態？ | task store、memory refs、workspace edits |
| resources | 需要、保留、實際用了什麼？ | token、time、memory、network、child slots |
| interruptible | pause/stop 後能否 resume/rollback/reset？ | tool cancel、Round checkpoint |
| guarantee | 可否重試、是否 transactional、可否 dry-run？ | send once、patch proposal |
| composition | 多節點合起來後性質如何？ | pipeline、team、nested agent |
| environment | 在哪個 namespace／sandbox 求值？ | mounts、container、network policy |
| nondeterminism | 哪些輸出含模型隨機性，證據為何？ | model reply、AI summary、grounding |

## 三種 metadata 不可混在一起

```text
declared  = 作者聲稱這個節點需要／保證什麼
effective = supervisor 實際授予並套用什麼
observed  = runtime 量到的用量、失敗率、延遲與可靠度
```

建議 projection：

```text
/tools/search/.meta/declared.json
/tools/search/.meta/effective.json
/tools/search/.meta/observed.json
/self/.agent/permissions/effective.json
/self/.agent/resources/{limit,reserved,used}
```

requested mount/network 不能出現在 effective view 裡冒充已隔離；模型的 `reliability` 也不能冒充證書。`nondeterministic` 證書是某次認證的凍結 evidence，`reliability` 是會隨觀測更新的活值。

## 組合不是把 JSON merge 起來

leaf 只描述自己；caller/supervisor 依 call graph 推導整體 effective properties。不同軸的合成規則不同：

- `state`、`nondeterministic`：通常是污染型；任一必要 hop 有，整條路就有。
- `guarantee`、`interruptible`：通常取最弱承諾；除非 wrapper 有可驗證的 rollback/guard 能 lift。
- `resources`：sequential 可取峰值，parallel 要加總；token budget 需 reserve，不能只估計。
- `entries`：依 wiring 產生新 interface，不是所有 entry 的聯集。
- `lifecycle`：由外層調度語意決定；persistent child 不必使 one-shot caller 永不結束。
- `environment`：取實際執行 backend 與 namespace view，不能由 child 自述。

每個 wrapper 對每一軸應明說 `pass`、`degrade` 或 `lift`。缺席不一定等於最強，也可能只是「不主張」；這是 composition 最容易撒謊的地方。

## 九軸也適用於記憶處理

`memory_promote`、summary generator、context compiler 不只是內部 helper，也應能自述：

- promotion 是否只讀、是否改 alias、能否 transactional。
- source span 與 object write 是否可安全重試。
- compiler 是否 deterministic；若使用 LLM relevance ranking，該節點就是 nondeterministic。
- 一個 link 展開的 bytes、depth、token resource 上限。
- 被 cancel 時是否留下半成 manifest。

特別是 AI 產生的 summary／grounding：正文 object 可以完全保存，但「這段在說什麼」仍是隨機推論。它應帶 model、prompt/policy version、source hashes、測試或人工審核狀態，不能因被寫成檔案就變成確定事實。

## 九軸與權限不是同一件事

九軸描述節點的性質；grants 描述某 actor 能不能用它。`resources.network=true` 是需求，不是網路授權；`entries.write` 是介面能力，不表示任何 peer 都能 write。

可以把授權看成掛載前的 type check：

```text
node declared requirements
∩ parent delegated capabilities
∩ runtime backend support
∩ task scope
= effective mount / or fail closed
```

若 backend 不支援宣告的隔離或中斷保證，正確結果是拒絕或降級且明示，不是把 declared 原樣投影成 effective。

## 一個實用結果

agentfs 的 `.meta/` 不只是說明頁；它讓 scheduler、context compiler、team layer 與人類能用同一套問題檢查任何 callable node。真正的「Lisp 式閉合」不是所有東西都長得像括號，而是：

> agent、tool、memory transform 與組合後的 team，仍可被同一組契約描述、驗證與再組合。
