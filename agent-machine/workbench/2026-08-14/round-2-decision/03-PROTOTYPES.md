# 可證偽原型路線

> **工作稿、非正式 AOS ABI。** 每級只驗一個新 seam；prototype evidence 不自動升級為產品承諾。

## 總覽

| 級別 | 狀態／只回答什麼 | 明確不做 |
|---|---|---|
| P0 process adapter | 已有 Python/C++ staging evidence | Task Receipt、composite、Agent |
| P1a-1 v2 | 已驗 Task-local Call與 child relation/replay | root registry、B1 re-stage、真 process |
| P1a-2 Phase 1（root accept/recover） | 已通過獨立 audit；僅驗 root registry／accept／recover（47 tests × 4 runs = 188 executions） | child relation、process、C++、CAS |
| P1a-2A（composite fake） | 已通過最終驗收：WSL `/tmp` 全套 58/58；兩個不啟動程式的假 child 保存／恢復 | process、dispatch、attempt、P0、effect、Agent、scheduler |
| P1a-2 Phase 2（process seam） | 已驗窄切片：`first` 為一個 P0 process、`second` 為 fake；全工作台 74/74，其中 process tests 16 | 完整 crash matrix、common ABI、Agent |
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

證據：[`v2 README`](../p1a-task-tree-python-v2/README.md)、[`NOTES`](../p1a-task-tree-python-v2/NOTES.md)、[`Opus review`](../opus-p1a-review/OPUS-REVIEW.md)。README 已改成歷史前置證據；NOTES 保留當時研究語境。

## P1a-2A：先補 composite fake

新的 `sequence_two_fake` 已通過最終驗收：在 WSL Ubuntu、Linux `/tmp` 工作目錄執行全套 **58/58**。它的兩個假 child 都只回傳固定資料、不啟動程式。測試確認「root registry → two fake children → root Receipt」的持久順序、10 個 root 與 23 個 composite 中斷切點後的穩定恢復；也確認恢復只讀已凍結的 root Call，以及容量邊界、暫存碰撞、孤兒不可自行進入 Task tree、已提交資料遭竄改時停止。收尾摘要見[當日工作台入口](../README.md)。

這項證據只適用於同一 Linux 檔案系統、單一寫入者，以及以終止行程／故障切點模擬中斷的工作台。它不改既有 `sequence_two`，不把假的 first 當成 process 成功，也沒有驗真 process、P0 執行嘗試與證據、Agent 或正式排程器；格式仍是工作台格式，不能提升為正式 ABI。規格／格式分見 [`09`](09-P1A2-COMPOSITE-FAKE.md)／[`10`](10-P1A2-COMPOSITE-FAKE-FORMATS.md)。

## P1a-2 Phase 2：單一 P0 case 的窄證據

現有 Python 工作台已有一條受控路徑：frozen root recipe → first 的一個 P0 invocation → strict binder → Receipt → second fake → composite Receipt；也驗 after-spawn side effect、不完整 repair、frozen recipe 與若干 fail-closed 反例。精確測試數、已驗邊界與未驗矩陣只有 [`Phase 2 證據帳`](../p1a2-process-python/PHASE2-EVIDENCE.md) 為準。

下列仍是目標工作契約，**不是宣稱已完整驗收的矩陣**：

1. Frozen root Call保存first/second recipes與oracle；root registry、same-ID re-stage先通過。
2. `first` child plan前才解析absolute executable並計算generation；pre-plan可re-stage，planned後freeze。
3. Task `events.jsonl`的 `dispatch_intent(attempt-1)`是唯一 attempt authority；P0 UUID只是 `attempts/attempt-1/p0/`下的 raw location，0 UUID repair、無 Task intent卻有 UUID或 >1 UUID皆 corruption。
4. Call stdin用明確base64；policy只支援inherit。Acceptance request非inherit須在root plan前拒絕，0 Task／dispatch；planned artifact若出現其他policy是corruption。P0未存actual env，不宣稱隔離或可重現。
5. P1a-2自有strict零寫入binder回Known／Incomplete／Contradiction三態；runner只重用P0 `FunctionStore.run`，不呼叫`recover()`。
6. Receipt是 `payload/<hash>.receipt.json`＋唯一 `receipt_committed{hash,size}` event，basis直接綁 attempt/P0 UUID/evidence hash；沒有額外 evidence authority。
7. known non-success仍建leaf Receipt並observe；root穩定等待parent policy，不跑second。

### P1a-2 停止線

- B1/B2先有新 failpoint tests；再驗 generation/env、Task intent與 UUID grammar、P0三個現有 hooks、artifact injection、真 after-spawn fixture、Receipt commit/tamper與 known-never-respawn。
- 目前只覆蓋代表性 root-planned handoff 與 after-spawn；10 個 root cuts、三個 P0 hooks、process golden vectors與所有 raw/torn 反例仍未串成完整 Phase 2 matrix。
- normal case 仍只有 root、first、second；unknown case仍是 first block second。
- 不同時設計 common ABI、Definition directory tree identity、dependency snapshot、GC、public accept idempotency 或 C++ writer。

## 後續 C++ seam

Python 語意通過後，才考慮讓 C++／其他 executor 成為 `ValidatedProcessEvidence` producer。C++ 不可直接寫 Task events、commit Task Receipt 或擁有 relation；否則 process adapter 會越過 Runtime authority。

## P1b／P1c／P2

- **P1b：**mock `Round Task -> Step Task -> Tool Task -> Step Task`，驗 Step 的原子觀察／pause boundary；child unknown 阻擋 Round。
- **P1c：**比較`aos exec ./ask`／directory/PATH、agent capacity／queue方案、checkpoint→Git→clone detached→attach。
- **P2：**以多 roots/families/queues 比較中央 manager/store/executor。manager 必須 recover 完才 ready，drain 後不收新 claim。

每級報告只聲稱實際 fault 模型；process-kill 不可寫成 power-loss durability。
