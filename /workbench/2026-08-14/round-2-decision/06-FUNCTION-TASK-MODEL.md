# Function／Task 與 P1a-2 authority

> **目標契約、非正式 ABI。** v2只驗 Task-local Call與 child relation/replay；B1/B2與 process seam待 P1a-2實作。process evidence細節見 [`07`](07-P1A2-PROCESS-SEAM.md)。

## 1. 物件與唯一 authority

```text
Definition + immutable Call --accepted--> Task
known Return + execution basis --receipt_committed--> Task Receipt
unknown                                      --> no Receipt
```

- Call是純請求：resolved Definition、argv、stdin bytes、cwd、environment policy、policy；不含 Task/parent/slot/state。
- `call_ref{hash,size}`是 canonical Call bytes identity；每 Task的 `call.json`只是 P1 resolver location。same Call可形成不同 Tasks。
- Function Return是 caller-facing result。Task Receipt綁 `task_id + call_hash + basis + Return`；unknown無 Receipt。

P1a-2 target layout：

```text
<store>/roots.jsonl                         # root relation authority
<store>/tasks/<id>/
  call.json
  task.json                                 # task_id + call_ref
  events.jsonl                              # 唯一 Task attempt/state authority
  attempts/<attempt-id>/p0/<uuid>/...       # location/raw process evidence
  payload/<hash>.receipt.json               # content-addressed Receipt bytes
```

沒有 attempt-local intent file或額外 evidence authority。P0 invocation自己的 `dispatch-intent.json`只證明該 invocation的 process intent，不能創造或回推 Task event。

## 2. B2 root入口：accept 與 recover分開

本 fixture只有兩個內部入口：

```text
accept_fixture_root(root_id, resolver)  # 顯式新接受／re-accept
recover_store()                         # 不接受、不 resolve的純 recovery入口
```

它們不是 public API；本片不承諾 idempotency、request ID或正式 root ID policy。fixture grammar只允許設定中的唯一 `root_id`。

`accept_fixture_root`次序：

1. resolver產生完整 Call並 stage於該 root Task directory；planned前 bytes可替換。
2. registry commit `root_planned{root_id,call_ref:{hash,size}}`；此刻 Call freeze。
3. 寫 `task.json`，Task append `accepted`。
4. registry commit matching `root_linked{root_id,call_ref:{hash,size}}`；此後 eligible。

若 crash在第 1步後、`root_planned`前，`recover_store()`看到 **0 roots**，忽略 staged directory；它不得呼叫 resolver、推測接受或清掉 staging。只有外部再次顯式呼叫 `accept_fixture_root`且傳同一 fixture `root_id`，才可重新 resolve並替換 pre-planned Call。這是內部測試操作，不是 public idempotency。

`root_planned`一旦 commit，`recover_store()`才有 authority補 `task.json → accepted → root_linked`；不重新 resolve。planned後 call bytes/ref mismatch是 corruption。

`roots.jsonl` grammar：唯一 root ID、嚴格 `planned → linked`、兩事件完整 `call_ref`相同；duplicate、逆序、未知/缺欄、完整 newline壞 JSON均 corruption。無 newline的 final torn tail先只當未提交；必須在 registry、Tasks、relations與Receipts的 committed projection整體合法後，recovery才可 truncate＋fsync，再重驗。

## 3. Child stage不是 root accept

child的 upstream是已接受 parent的 stable slot，不使用 `accept_fixture_root`。parent durable plan能確定下一 slot時，progress可產生/re-stage child Call；parent commit：

```text
child_planned{slot,task_id,call_ref:{hash,size}}
  -> child task.json -> child accepted
  -> child_linked{slot,task_id,call_ref:{hash,size}}
  -> eligible
```

planned前 child staging可替換；planned後 freeze。root pre-planned staging因沒有 durable parent/slot而只能等顯式 re-accept，兩者 recovery規則不可混用。parent ordered events是唯一 child relation authority；child backlink只能是 projection。

只有 staged Call、未被 root/child planned event引用的 directory不是 Task、不是 corruption、不得 dispatch。planned-but-unlinked已有 authority，**不是 orphan且不可 GC**；recovery須補 materialize/accepted/link。P1a-2不做 GC。

## 4. Task event grammar

leaf只有兩支合法主線：

```text
accepted -> repair_required(reason=generation|unsupported_environment)
             # dispatch_intent count = 0

accepted -> dispatch_intent{attempt_id}
         -> receipt_committed{receipt_hash,size}
          | repair_required(reason=missing_or_invalid_evidence)
```

`events.jsonl`的 `dispatch_intent{attempt_id}`是唯一 Task attempt authority。attempt directory、P0 UUID或 P0 intent都不能補寫、推導或取代它：

- Task intent存在但 attempt下 **0 UUID**：結果不明，repair/no Receipt/no retry。
- 沒有 Task intent卻有任一 P0 UUID：corruption，表示 evidence越過 eligibility/intent。
- 同 attempt有 **>1 UUID**：corruption，不挑一個也不 retry。
- Task intent＋唯一 UUID：由 [`07`](07-P1A2-PROCESS-SEAM.md) strict binder判斷完整 evidence或 repair。

generation與 environment policy只在 Task intent前驗。intent後 Definition改變不得推翻完整舊 evidence；若 evidence缺失仍 repair。

## 5. Receipt authority

Receipt canonical bytes先發布為 `payload/<sha256>.receipt.json`；event前它只是可重建 staging，不代表完成。唯一 semantic completion是 Task event：

```text
receipt_committed{receipt_hash,size}
```

leaf Receipt basis直接含：

```text
{attempt_id, p0_invocation_id, evidence_hash}
```

raw evidence完整時可重建完全相同 Receipt；crash後只補 content-addressed payload或 commit event，不 respawn。commit前未引用/錯誤 staging可丟棄重建；commit後 payload、basis或 raw evidence tamper一律 corruption。

composite Receipt basis保留 ordered slot：

```text
children:[{slot,task_id,receipt_ref:{hash,size}}, ...]
```

parent只有驗過 child `receipt_committed`及 payload後才 append `child_observed{slot,task_id,receipt_ref}`；不複製 child Return。first observed後才可 plan second；unknown child阻擋 parent Receipt與 second dispatch。

## 6. P1a-1 v2 證據界線

**P1證據：**v1已被 v2取代。現有 suite有 18 test methods／23 failpoints，支持 Task-local Call、same Call/different Tasks、parent向下 replay、unlinked不 dispatch、known Receipt不重做、strict validation與 torn-tail處理。

**未驗：**v2沒有 root registry，仍由固定 ID的 `initialize()`建立；不同 pre-planned bytes會 corruption。B1/B2是 Opus准入條件，B4只有 unlinked不 dispatch獲證據。來源：[`v2 README`](../p1a-task-tree-python-v2/README.md)、[`NOTES`](../p1a-task-tree-python-v2/NOTES.md)、[`Opus review`](../opus-p1a-review/OPUS-REVIEW.md)。前兩者是 review前記錄。

fault model只有 WSL `/tmp` process-kill/replay，不是 power loss。full replay成本、單 writer、`lstat`非 TOCTOU、CAS/GC、public accept、common Return ABI、P1b/P1c/P2仍待後續。
