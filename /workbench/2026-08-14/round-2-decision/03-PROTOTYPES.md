# 可證偽原型路線

> **工作稿、非正式 AOS ABI。** 每級只驗一個新 seam；prototype evidence 不自動升級為產品承諾。

## 總覽

| 級別 | 狀態／只回答什麼 | 明確不做 |
|---|---|---|
| P0 process adapter | 已有 Python/C++ staging evidence | Task Receipt、composite、Agent |
| P1a-1 v2 | 已驗 Task-local Call與 child relation/replay | root registry、B1 re-stage、真 process |
| P1a-2 phase 0 | 先完成 Opus B1/B2，固定 B4規則 | process、C++、CAS |
| P1a-2 seam | 再只換 `first` 為 P0 process | 完整 process matrix、Agent |
| P1b | mock Agent Round 的 Step/Tool/pause | Git、PATH、真 LLM |
| P1c | agent root UX／portability | 中央公平排程、跨機 |
| P2 | central Runtime 具體形狀 | network service／sync |

## P0：process staging evidence

P0 Python 已驗 no-shell argv、raw stdin/stdout/stderr、雙 pipe、exit/signal/spawn error、artifact hash，以及 intent 後沒有可信結果時不重跑。其目錄與名為 `receipt.json` 的檔案都是 **P0 process staging evidence**；P0 `outcome_unknown` projection 不是 AOS Task Receipt。

```text
leaf Task -> attempt -> P0 staging -> validate/bind -> Task Receipt
```

證據：[`P0 README`](../p0-function-python/README.md)、[`P0 NOTES`](../p0-function-python/NOTES.md)。

## P1a-1 v2：已驗證的範圍

### 狀態

v1 已由 v2 取代。現有 v2 suite有 **18 個 test methods／23 個 failpoints**；matrix在 WSL `/tmp` 以 process kill中止 writer，再由十個新 process replay。這不是 power-loss、device-cache、NFS 或多 writer測試。

### 已支持

```text
sequence-two root Task
  "first"  -> fake leaf Task
  "second" -> fake leaf Task
```

- 每 Task 自存純 Call；`task.json` 只綁 `task_id` 與 `call_ref{hash,size}`。
- first／second 的 Call bytes/hash 相同但 Task ID 不同，relation 沒滲入 Call。
- parent ordered events 是 child relation authority；unlinked call-only directory不進 tree、不 dispatch。
- known evidence recovery 只補投影；unknown 無 Receipt，first／parent repair，second dispatch count `0`。

現有 v2 **沒有** durable root registry；root由固定 ID的 `initialize()`建立。它也未驗 pre-planned definition改變：既有不同 staged bytes會 corruption。故 B1/B2是 Opus准入條件，須在接 process前完成；B4只有 orphan不 dispatch獲得證據，GC規則仍是文件契約。另有 full replay成本與 `lstat`非 TOCTOU限制。

證據：[`v2 README`](../p1a-task-tree-python-v2/README.md)、[`NOTES`](../p1a-task-tree-python-v2/NOTES.md)、[`Opus review`](../opus-p1a-review/OPUS-REVIEW.md)。前兩份是 review前記錄，仍寫「等待 Opus」。

## P1a-2：只接一個 P0 case

唯一完整規格在 [`07-P1A2-PROCESS-SEAM.md`](07-P1A2-PROCESS-SEAM.md)。先實作 B1/B2並補 failpoints，再接 process：

1. `first` 改成 deterministic process leaf；`second` 保持 v2 fake。
2. accept/planned 前解析 fixture executable absolute path並計算 content SHA-256 generation；Call 保存結果。
3. Task `events.jsonl`的 `dispatch_intent(attempt-1)`是唯一 attempt authority；P0 UUID只是 `attempts/attempt-1/p0/`下的 raw location，0 UUID repair、無 Task intent卻有 UUID或 >1 UUID皆 corruption。
4. Call stdin用明確 base64 bytes；environment policy只支援 inherit。P0未保存 actual env，故不宣稱隔離/snapshot/reproducibility；非-inherit在 intent前 repair/dispatch 0。
5. P1a-2自有 strict零寫入 binder比對 request/raw streams/termination，算 domain-separated manifest evidence hash；runner只重用 P0 `FunctionStore.run`，不用 CLI、不複製、不呼叫 `recover()`。
6. Receipt是 `payload/<hash>.receipt.json`＋唯一 `receipt_committed{hash,size}` event，basis直接綁 attempt/P0 UUID/evidence hash；沒有額外 evidence authority。
7. parent oracle只走 exited0/expected/empty；nonzero/signaled/spawn_error仍是完整已知 leaf Returns，composite mapping延後。

### P1a-2 停止線

- B1/B2先有新 failpoint tests；再驗 generation/env、Task intent與 UUID grammar、P0三個現有 hooks、artifact injection、真 after-spawn fixture、Receipt commit/tamper與 known-never-respawn。
- normal case 仍只有 root、first、second；unknown case仍是 first block second。
- 不同時設計 common ABI、Definition directory tree identity、dependency snapshot、GC、public accept idempotency 或 C++ writer。

## 後續 C++ seam

Python 語意通過後，才考慮讓 C++／其他 executor 成為 `ValidatedProcessEvidence` producer。C++ 不可直接寫 Task events、commit Task Receipt 或擁有 relation；否則 process adapter 會越過 Runtime authority。

## P1b／P1c／P2

- **P1b：**mock `Round Task -> Step Task -> Tool Task -> Step Task`，驗 Step 的原子觀察／pause boundary；child unknown 阻擋 Round。
- **P1c：**驗 `aos exec ./ask`／directory/path sugar、agent capacity 1、queue、checkpoint→Git→clone detached→attach。
- **P2：**以多 roots/families/queues 比較中央 manager/store/executor。manager 必須 recover 完才 ready，drain 後不收新 claim。

每級報告只聲稱實際 fault 模型；process-kill 不可寫成 power-loss durability。
