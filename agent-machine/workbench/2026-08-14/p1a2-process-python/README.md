# P1a-2 process / fake 工作台

這是 Linux `/tmp` 上的單一 writer 斷點恢復工作台，**不是正式 AOS ABI、scheduler 或 `.aos` 布局**。root 是 Task tree 的頂層 Task，不是 agent root；`sequence_two` 也不是 Step 或 Round。

## 入口

- [PHASE2-EVIDENCE.md](PHASE2-EVIDENCE.md)：唯一精確的 Phase 2 證據、測試數、已驗窄切片與未驗矩陣。
- [aos_p1a2.py](aos_p1a2.py)：Phase 1 root `accept` / `recover` CLI。
- [p1a2_model.py](p1a2_model.py)：root registry／accept／recover。
- [p1a2_composite_fake.py](p1a2_composite_fake.py)：兩個不啟動程式的 fake child。
- [p1a2_process_runtime.py](p1a2_process_runtime.py)：Phase 2 Python API；first 是 P0 process、second 是 fake。
- [p1a2_process_binder.py](p1a2_process_binder.py)：P0 raw artifact 的唯讀 Known／IncompleteUnknown／Contradiction binder。

精確工作台格式與待驗契約見 [`06`](../round-2-decision/06-FUNCTION-TASK-MODEL.md)、[`07`](../round-2-decision/07-P1A2-PROCESS-SEAM.md)、[`08`](../round-2-decision/08-P1A2-FORMATS.md)、[`09`](../round-2-decision/09-P1A2-COMPOSITE-FAKE.md) 與 [`10`](../round-2-decision/10-P1A2-COMPOSITE-FAKE-FORMATS.md)。實作證據不能反向提升這些工作稿為正式 ABI。

## 窄流程

```text
root registry -> root accepted/linked
  -> first process Call -> dispatch_intent -> one P0 invocation
  -> read-only bind -> process Receipt -> child_observed
  -> second fake Receipt -> root composite Receipt
```

`child_planned` 前可由 frozen root recipe materialize first；plan 後 Call/ref freeze。process 的 generation mismatch 或不完整 raw 都沒有 Receipt，且不會規劃 second。完整 raw 的 Receipt commit 前中斷，恢復只能重建同一 Receipt，不能重跑 P0。

## 重跑

```bash
cd agent-machine/workbench/2026-08-14/p1a2-process-python
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -v
```

測試與大小 gate 都在工作台內；新增／修改檔各不超過 8192 bytes。結論與未驗範圍以 [PHASE2-EVIDENCE.md](PHASE2-EVIDENCE.md) 為準。
