# 已驗／未驗：綠燈只代表跑過的那一小片

這是 [`full/12`](../full/12-IMPLEMENTATION-AND-EVIDENCE.md) 與 2026-08-14 工作台的證據快照。數字用來界定範圍，不是正式 ABI 的版本號。

## 目前留下的實測證據

| 切片 | 實際做了什麼 | 可以聲稱 | 不能聲稱 |
|---|---|---|---|
| P0 Python | `shell=False` 啟動 leaf；分開 argv、raw stdin／stdout／stderr、exit／signal／spawn error | intent 後缺可信結果時不自動重跑 | Task、排程器、正式 Receipt |
| P0 C++23 | `fork/execve`、nonblocking pipes、`poll`；含 16 MiB delayed-read、雙大量輸出與 12 次重跑 | 三條串流必須同時推進；分開 exit、signal、`chdir/exec` error | hash／fsync durable writer、timeout、正式 schema |
| P0 Janet | Windows Janet `1.41.2-local`，25 tests | 純資料 admit／audit／policy 可行 | Linux mechanics、spawn、正式提交權 |
| P1a-1 v2 | fake executor；18 test methods、23 failpoints，每個切點由 10 個新 process replay | Task-local Call、parent relation、orphan 不 dispatch、known 不重做 | root accept、真 process、Agent |
| P1a-2 Phase 1 | 47 tests × 4 runs = 188 executions | root registry／accept／recover | child relation 或 process execution |
| P1a-2A | WSL Ubuntu、Linux `/tmp`，全套 58/58；10 個 root 加 23 個 composite 中斷切點 | root → first → second 的兩個 fake child 保存／恢復、來源封閉、容量與竄改反例 | 真 process、P0 Attempt、Agent、正式排程器 |
| [P1a-2 Phase 2 本輪窄切片](../workbench/2026-08-14/p1a2-process-python/PHASE2-EVIDENCE.md) | WSL Ubuntu、Linux `/tmp`，全套 74/74；其中 16 個 process tests | 第一個真 process、完整 raw 重開補提交、after-spawn 不重跑、已凍結 recipe／外部作用屏障／矛盾防護線 | 完整 Phase 2、正式排程器、Agent |

P1a-2A 的兩個 child 都只回固定資料，不啟動程式；後續 Phase 2 窄切片才把 `first` 換成真 process，`second` 仍保持 fake。74/74 是整套回歸結果，16 個 process tests 才是本輪新增的直接證據；不能把總數寫成完整 Phase 2 已驗。

## 本輪真 process 窄 seam

本輪只把 `first` 換成真 process，`second` 仍保持 fake：

```text
root
  first -> process leaf -> attempt-1 -> 一個 P0 invocation
  second -> fake leaf
```

已驗主線沿用以下安全順序：

```text
accepted + linked
-> generation check
-> durable dispatch_intent(attempt-1)
-> P0 runner 恰好呼叫一次
-> 唯讀 binder 驗證原始證據
-> Receipt payload
-> receipt_committed
-> parent child_observed
```

Binder（證據綁定器）不能寫入；它只把原始資料判成：完整且一致、intent 後合法但不完整，或互相矛盾。第二種停在 unknown／repair，不重跑；第三種直接 corruption，不可用 repair 洗掉。

### 這 16 個 process tests 支持什麼

- `first` 真正啟動一次 Linux process；成功後提交 leaf Receipt，再執行 fake `second` 並形成 composite Receipt。
- 七件完整 P0 raw 已存在但 Task Receipt 尚未提交時，recovery 只重建相同 Receipt；P0 UUID 與外部副作用次數不增加。
- After-spawn 已看見副作用、writer 被殺且原始證據不完整時，recovery 提交 incomplete repair，不再啟動 process。
- Frozen root recipe 或已規劃 Call 遭替換、effect barrier 前出現越權下游資料、request／stream／hash／檔案形狀互相矛盾時，會在 Receipt 前 fail closed。
- `accepted -> dispatch_intent -> repair_required(generation)` 被明確拒絕：dispatch 後可能已有 effect，不能再把它洗成「尚未執行的 generation repair」。

### 還沒補齊

- P0 三個既有 hooks 的完整中斷矩陣。
- 十段 root failpoint 與真 process 串接的完整矩陣。
- Phase 2 對已知 nonzero、signal、`spawn_error` 的自動測試。
- Process Call、evidence manifest 與 Receipt 的 checked-in golden bytes（寫死在測試裡的逐 byte 預期資料）。

所以目前可以說「第一個真 process 的最低安全反證門檻已通過」，不能說「完整 Phase 2 已完成」。

## 五個最容易過度推論的地方

1. P0 的 `receipt.json` 只是 process staging evidence，不是 AOS Task Receipt。
2. Nonzero、signal、launch error 可是完整已知 Return；unknown 是缺證據，不是已知失敗。
3. Dispatch 前 hash 過 executable，不代表實際 `exec` 的 bytes 沒被替換；check-to-exec 仍有 TOCTOU。
4. 單一 lock 的競爭測試不等於多寫入者安全。
5. Process-kill／failpoint 不等於斷電、NFS、恰好一次或外部作用回滾。

## 提升前至少要有什麼

- 白話語意，不靠某個 JSON 欄位才能解釋。
- 能推翻設計的反例與中斷恢復案例。
- 明列 OS、檔案系統與 writer 假設。
- C++ 守住 process、FD、lock、`fsync`、證據重驗與正式 commit。
- 要成為可攜格式時，跨實作 golden bytes 一致。
- 文件只提升已驗邊界，原型格式留在工作台。

完整原文：[`full/02`](../full/02-LEAF-PROCESS-CALL.md)、[`04`](../full/04-COMPOSITE-FUNCTION-AND-TASK-TREE.md)、[`12`](../full/12-IMPLEMENTATION-AND-EVIDENCE.md)、[`13`](../full/13-LIMITS-AND-LATER.md)。實測細節見 [P0 Python](../workbench/2026-08-14/p0-function-python/README.md)、[P0 C++](../workbench/2026-08-14/p0-function-cpp/README.md)、[P0 Janet](../workbench/2026-08-14/p0-function-janet/README.md)、[P1a-1 v2](../workbench/2026-08-14/p1a-task-tree-python-v2/README.md)、[P1a-2 Phase 2 證據帳](../workbench/2026-08-14/p1a2-process-python/PHASE2-EVIDENCE.md)與[真 process 工作契約](../workbench/2026-08-14/round-2-decision/07-P1A2-PROCESS-SEAM.md)。
