# P1a-2 Function／Task authority

> **待驗工作契約、非正式 ABI。** Exact JSON與 ID格式見 [`08`](08-P1A2-FORMATS.md)，process leaf見 [`07`](07-P1A2-PROCESS-SEAM.md)。

## 0. 本片沒有定義什麼

- 本頁的 **root** 是 Task tree的 top-level Task；`root_id == root Task ID`。它不是 agent root或 agent絕對路徑 ID。
- `sequence-two` 是一般 composite Function fixture，不是 Step／Round，也不定 Step原子邊界。
- `<store>` 是 machine-local crash workbench，不是正式 `.aos`、Git可攜 Task image、agent repo或 filesystem memory的完整布局。
- `recover_store()` 只驗一條恢復入口，不是 AOS完整 start/recover/ready/drain/stop生命週期。
- immutable Call、accepted後的 durable Task record、Receipt與下列 events都是 **P1暫選**。

## 1. Authority與布局

```text
<store>/roots.jsonl
<store>/tasks/<task-id>/
  call.json
  task.json
  events.jsonl
  attempts/<attempt-id>/p0/<uuid>/...
  payload/<sha256>.receipt.json
```

- `call_ref{sha256,size}`是 canonical Call bytes identity；`call.json`只是P1 location。
- `task.json`只綁`task_id + call_ref`。`events.jsonl`是唯一 Task attempt/state authority。
- root relation只由`roots.jsonl`保存；child relation只由parent ordered events保存。child backlink與report都只是可重建 projection。
- Receipt payload在commit event前只是staging。唯一完成authority是`receipt_committed{receipt_ref}`。
- 所有驗證從 committed `root_planned`與`child_planned`可達集合開始；只有 matching `*_linked`後才eligible。不得掃`tasks/`猜root或relation。
- accept、progress、recover共用store exclusive lock；recovery開始前舊Runtime與worker必須已停止。多writer不在本片。
- Store開啟的所有fd（含lock、log、temp與directory fd）須立即設non-inheritable／`FD_CLOEXEC`；spawn採`close_fds=true`且不得傳任何store fd。

## 2. Frozen root Call是child的持久來源

root Call保存prototype-only static recipes：

```text
first  = process path + argv + stdin + cwd + inherit policy + success oracle
second = fake Return
```

`kind:"sequence_two"`是prototype builtin Definition identity。Recipes／oracle只是fixture暫寄在Call的參數，納入`call_ref`但不是relation、plan或state，也不含child Task ID。child ID由`derive_child_id(root_id,slot)`算出。

只有顯式`accept_fixture_root(root_id, acceptance_resolver)`可產生新root Call。`recover_store()`與後續progress不得呼叫外部acceptance resolver；它們只讀已accepted root Call recipe。

parent已accepted且linked、下一slot尚未planned時，progress由frozen recipe materialize child Call：

- `first`在child plan前讀目前executable並計算generation；pre-plan staging可因crash或Definition改變而整份re-stage。
- `second`由frozen fake recipe產生。
- `child_planned` commit後Call bytes/ref freeze；runtime relation仍只在parent events。

## 3. Root accept與same-ID re-accept

持exclusive lock後，accept必須依序：

0. 先驗root ID格式且不得含`--`；不合即在resolver、candidate path與任何authority write前拒絕。
1. **純讀**驗證roots committed prefix、所有planned relation、所有reachable Task／Receipt，以及精確`tasks/<root_id>` candidate。
2. 只有整體projection合法，才修復可安全辨識的final torn tails並fsync；之後重新全驗。
3. 若registry已有該`root_planned`，在呼叫resolver或任何write前拒絕。
4. 若candidate未被任何planned event引用，只接受：空目錄，或只含regular non-symlink `call.json`／允許的atomic temp。出現`task.json`、events、attempts、payload、子目錄或未知entry即回報anomaly，絕不刪除。
5. resolver成功產生valid root Call後，才以整個candidate staging重建；publish Call後commit `root_planned(root_id,call_ref)`。
6. 寫matching `task.json`、append唯一`accepted`，最後commit matching `root_linked`。

第4–5步就是本片唯一允許的orphan替換：必須是**顯式、same-ID re-accept**且candidate形狀合規。其他unreferenced orphan一律保留。未來store maintenance也只能在exclusive lock與完整roots/relation驗證後清理；planned-but-unlinked永不可清。

## 4. Planned但尚未linked

`root_planned`或`child_planned`一旦commit，Call已freeze且Task已有authority。unlinked白名單只有：

```text
matching call.json
optional matching task.json
optional events.jsonl containing exactly one accepted
```

不得有其他event、attempt、UUID、payload或child relation。Recovery可依序補missing `task.json -> accepted -> linked`。如果`linked`已存在卻缺`task.json`或`accepted`，是corruption，不能倒推重建。Call bytes、`task.json`或planned/linked refs不一致也是corruption。

## 5. Child relation與穩定停止

```text
child_planned(slot,task_id,call_ref)
 -> child task.json -> accepted
 -> child_linked(slot,task_id,call_ref)
 -> child Receipt commit
 -> child_observed(slot,task_id,receipt_ref)
```

parent必須驗完整child Receipt後才observe。`first`符合fixture success oracle才可plan `second`；second完成後root才建composite Receipt。

Second fake是deterministic materialization；其唯一自身event線是`accepted -> receipt_committed`，沒有`dispatch_intent`、`attempts/`或P0 UUID。Parent之後append`child_observed(second)`，再建root composite Receipt。

First的三種穩定停止projection：

- `repair_required(generation)` → `waiting_for_child_repair_generation`。
- `repair_required(incomplete_evidence)` → `waiting_for_child_repair_incomplete_evidence`。
- known Receipt不符oracle → `waiting_for_parent_policy`。

前兩者parent不observe；known不符oracle先observe。三者都不plan second、不建root Receipt，repeat recovery零寫。詳見 [`07`](07-P1A2-PROCESS-SEAM.md)。

## 6. Recovery演算法

每輪固定：

1. 持lock且確認舊worker停止；全程純讀驗證registry、所有planned relation、reachable Task、P0目錄與committed Receipts。
2. 若只有合法final torn tails，先驗所有committed prefixes仍一致；再修tail並fsync，重新從第1步全驗。
3. 找出唯一下一個合法gap；最多補一個authority transition或一個staging＋緊接的plan commit。
4. 每次write後重新從第1步全驗，直到穩定。任何corruption立即停止。

Recovery不接受新root、不讀CLI參數、不用ambient設定重建recipe、不清orphan，也不能先補link再把越權的下游資料洗成合法。

## 7. Durable publish與effect barrier

- immutable file／payload：同目錄temp → write全bytes → file fsync → atomic rename → directory fsync。
- JSONL commit：一次append完整canonical JSON＋newline並file fsync；只有最後無newline片段可視為torn tail。
- directory建立／移除／交換後都fsync parent directory。
- 前一authority commit未成功前，禁止下一authority write或外部effect。
- Task `dispatch_intent` durable後才能建立P0 UUID／呼叫runner；Receipt payload durable後才能commit；commit durable後parent才能observe。
- Spawn必須`close_fds=true`；除stdin/stdout/stderr等process介面外，不得以`pass_fds`或其他方式洩漏store fd。
- I/O失敗立即停止，不跨越下一個barrier。

## 8. 證據界線

P1a-1 v2只驗Task-local Call與child relation/replay，沒有root registry、recipes或以上accept/recovery grammar。P1a-2只聲稱同一Linux filesystem上的process-kill/failpoint結果；不聲稱power-loss、NFS、TOCTOU、multiwriter、GC、portable checkpoint、Agent語意或完整Runtime。
