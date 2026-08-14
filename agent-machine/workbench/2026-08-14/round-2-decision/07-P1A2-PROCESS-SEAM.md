# P1a-2 process leaf 工作規格

> **待驗目標、非正式 ABI。** Authority與accept/recovery見 [`06`](06-FUNCTION-TASK-MODEL.md)；exact prototype格式見 [`08`](08-P1A2-FORMATS.md)。

## 0. 範圍

```text
sequence-two root Task（一般 composite fixture）
  first  -> process leaf -> attempt-1 -> one P0 invocation
  second -> fake leaf
```

- process Call與executable hash只適用 **first leaf**；不制定composite、directory或agent Function ABI，也不可外推到會自改的agent root。
- root是top-level Task（`root_id == Task ID`），不是agent root；`sequence-two`也不是Step／Round。
- 本片保持P0與P1a-1 v2不變，在新的`p1a2-process-python/`實作。

## 1. Root recipe與child Call

Frozen root Call保存first／second recipes與success oracle並納入`call_ref`，但不含child IDs／relations；exact shape見 [`08 §3`](08-P1A2-FORMATS.md)。Acceptance resolver只能在 [`06 §3`](06-FUNCTION-TASK-MODEL.md) 的全域驗證與already-planned拒絕後執行；recovery／progress只讀frozen recipe。

Pre-plan materialize first時驗recipe，把path解成absolute regular non-symlink executable、hash live bytes並stage process Call。`child_planned`前可整份re-stage；commit後freeze。

## 2. Generation與environment分界

寫`dispatch_intent`前最後檢查current path/type/hash：

- **live Definition bytes不同**：append `repair_required(reason=generation)`，Task intent與P0 UUID皆0。
- **frozen call.json、task.json、planned/linked call_ref不一致**：corruption；不可誤報generation。
- Acceptance request的policy非exact `{"kind":"inherit"}`：在`root_planned`前拒絕，0 Task／dispatch。若planned Call／recipe出現其他policy則是corruption，不是repair。

Generation僅證明dispatch前檢查過live bytes。原型假設最後check至七件raw完整間Definition不變，測試只能在raw完整後換檔；P0只記path，所以**不證明實際exec bytes**或TOCTOU安全。正式方案另比較snapshot／fixed FD。

Intent後若raw完整，不重hash current Definition；若不完整則repair且不respawn。P0未存actual env；binder只驗inherit，ambient probe只證明繼承，不宣稱隔離或可重現。

## 3. Dispatch次序與P0 seam

single-writer lock下的first次序：

```text
accepted + linked
 -> generation check
 -> durable Task dispatch_intent(attempt-1)
 -> 建立/驗證空 attempts/attempt-1/p0
 -> FunctionStore.run(Request(...)) exactly once
 -> read-only bind
 -> durable Receipt payload
 -> durable receipt_committed
 -> durable parent child_observed
```

Task intent durable後才可建立UUID／effect。Runner只import P0 `Request`與`FunctionStore.run()`；禁用P0 `inspect/recover/private validator`、CLI與runner複製。`attempts/.../p0/<uuid>` raw artifacts只准writer寫，受控Function process不得直接寫store。

P0現有三hooks：`after_intent`（spawn前）、`after_receipt_json`（七raw後、ready前）、`after_receipt`（ready後、terminal前）。

專用after-spawn fixture先fsync side-effect及其directory，再`SIGKILL` writer parent；它不是P0 hook，用來驗effect可見但不重跑。

Recovery不得與舊writer或受控fixture process group並存；process-recovery測試須先證明兩者都已消失，才可取得lock並讀store。

## 4. Strict read-only binder

Binder零寫入；ID／directory／artifact依 [`08§1、§5`](08-P1A2-FORMATS.md) 做direct-child、non-symlink、bounded、exact-schema／whitelist檢查。

Task attempt組合：

- 無Task intent卻有任一attempt／UUID：**Contradiction**。
- Task intent＋0 UUID：**IncompleteUnknown**。
- Task intent＋唯一matching immediate UUID：檢查raw。
- matching attempt有>1 UUID、其他attempt有UUID、非UUID entry、FIFO或symlink：**Contradiction**。

Binder只回三態：

1. **KnownEvidence**：七raw完整，request、Call、order、termination及streams一致。
2. **IncompleteUnknown**：intent後只有合法publish prefix／temp且無矛盾；append `repair_required(reason=incomplete_evidence)`，無Receipt／retry。
3. **Contradiction**：schema／identity／order／filesystem矛盾；store corruption，不寫repair。

Contradiction包括request/Call不同、JSON多/缺欄或duplicate、receipt先行stream遺失／hash錯、FIFO/symlink、UUID越權及P0 `outcome_unknown`；後者表示禁用的P0 recovery曾執行。

Commit前，合法缺檔才是IncompleteUnknown；tamper或倒序仍是Contradiction。Commit incomplete repair時以 [`08 §2`](08-P1A2-FORMATS.md) 的`attempts_ref`凍結當下projection；terminal repair後`attempts/`任何新增／改動都使ref不符，屬Contradiction。Generation repair後出現attempt亦同。`receipt_committed`後任何Call、raw、manifest或Receipt mismatch一律corruption。

## 5. Evidence、Receipt與parent結果

KnownEvidence依 [`08 §5`](08-P1A2-FORMATS.md) 算七檔`evidence_hash`；leaf Receipt綁Task／Call／attempt／UUID／evidence／Return。

發布順序固定：

1. 由完整raw產生deterministic Receipt bytes。
2. atomic publish `payload/<receipt-sha256>.receipt.json`並fsync。
3. append/fsync `receipt_committed{receipt_ref}`。
4. parent重驗child commit與payload，才append/fsync `child_observed{receipt_ref}`。

Crash在第1–3步只補相同payload／event，UUID set與side-effect count不變。

nonzero／signal／spawn error皆建leaf Receipt，parent一律observe：

- first符合`exited(0)+expected stdout+empty stderr`才plan second。
- repair(generation)：parent不observe，projection＝`waiting_for_child_repair_generation`。
- repair(incomplete_evidence)：parent不observe，projection＝`waiting_for_child_repair_incomplete_evidence`。
- known Receipt不符oracle：parent已observe，projection＝`waiting_for_parent_policy`。
- 上述三種等待都不plan second、不建root Receipt，repeat recovery零寫。
- second唯一自身event線是`accepted -> receipt_committed`，不建intent／attempt／P0 UUID；parent再`child_observed(second)`，最後以ordered refs建root composite Receipt。

## 6. 必測狀態與反例

### Authority／recovery

- root空／call-only staging及root/child planned後所有reachable gaps；unlinked白名單；linked缺task／accepted必corruption。
- roots／events在各event後及多logs同時torn。
- poison/counter resolver證明recovery不呼叫；child只用frozen recipe。
- same-ID合法staging可re-accept；其他orphan bytes不變。
- root ID含`--`在plan前拒絕且authority writes＝0；正常derived child ID仍可含`--`。

### Process／binder

- executable／argv／cwd／stdin mismatch；七raw逐件missing/tamper；JSON duplicate/extra/NaN/oversize、FIFO、symlink、錯ID與額外entry。
- 三個P0 hooks及after-spawn kill-parent。
- 啟動recovery process前證明舊writer與fixture process group皆不存在；terminal repair後注入／改寫attempt artifact必為Contradiction。
- generation在pre-plan、planned後intent前、raw完整後改變的三種不同結果。
- known nonzero／signal／spawn error後parent穩定waiting，second不存在。

每個failpoint必須精確exit `97`，並由test driver寫store外同層`<store-name>.failpoint-used` sidecar；它不是store內容／authority。wrong／unreachable hook不得算通過。每次crash前後同時比較：

```text
Task event counts + P0 UUID set + durable side-effect count
```

不能只看report。Golden vectors由實作產生實際canonical bytes/hash並鎖入tests，要求見 [`08 §7`](08-P1A2-FORMATS.md)。

## 7. 聲稱停止線

只聲稱WSL `/tmp`、single writer、process-kill/failpoint evidence。不得聲稱power-loss、TOCTOU安全、actual executable snapshot、environment reproducibility、common composite ABI、Agent語意、完整AOS lifecycle或C++ writer。
