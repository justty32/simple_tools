# P1a-2 Phase 2 證據帳

> **實作證據，不是正式 AOS ABI。** 本檔是本工作台唯一的精確 Phase 2 證據來源；[`README.md`](README.md) 只作入口。格式與工作契約仍見相鄰裁決稿，未被本檔升格為產品承諾。

## 執行結果

在 WSL Ubuntu、Linux `/tmp`、Python 單一 writer 下執行：

```text
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -v
Ran 74 tests
OK
```

其中 **16 個 process tests** 是 `test_process_*.py`：after-spawn 1、guardrails 5、Phase 2 端到端／binder 9、size gate 1。其餘是既有 root accept/recover、formats/vectors 與 composite fake 回歸；74/74 是整個工作台 suite，不是「74 個完整 Phase 2 crash cases」。

## 已驗的窄切片

| 類別 | 已驗事實 |
|---|---|
| 正常序列 | `sequence_two` 的 first 用 P0 `Request` / `FunctionStore.run()` 真正啟動一次；first oracle 成功後才 materialize second fake，最後 commit composite Receipt。 |
| commit/recovery | 七件 raw 完整但 process Receipt 尚未 commit 時，recover 只發布相同 Receipt／後續 projection；P0 UUID 與 fixture side effect 不增加。 |
| after-spawn | fixture 先 fsync append side effect 與目錄，再殺 writer parent。測試以新 session 啟動，確認 process group 消失才 recover；結果為 UUID=1、effect=1、incomplete repair，且 Receipt／observe／second 均為 0。 |
| frozen recipe | planned first Call 的 executable path、argv、stdin、cwd、inherit policy 都重驗對 frozen root recipe；偽造 Call 在任何 P0 effect 前 corruption。 |
| generation | accepted/linked 後 generation 改變，得到 `waiting_for_child_repair_generation`；沒有 intent/attempt。generation repair 後新增 attempts，或 intent 後才寫 generation repair，都會 corruption。 |
| binder | request/argv/cwd/stdin、raw publish prefix、future temp、stream hash、`outcome_unknown`、receipt version bool、非 object outcome 都 fail closed。合法不完整前綴才是 `IncompleteUnknown`。 |
| effect barrier | root premature payload、unlinked/pre-intent attempts 或 payload 都在 effect 前拒絕；端到端 raw tamper、request mismatch、outcome unknown 後比較 authority tree，確認 recover 零寫、零 Receipt、零 observe、零 second。 |
| root handoff | 代表性的僅 `root_planned` gap 由既有 `RootRuntime.recover_store()` 補到 linked，才進 process seam。 |
| 檔案界限 | size gate 掃本輪 `p1a2_process*.py` 與 `test_process*.py`；每檔不超過 8192 bytes。 |

所有上述只聲稱同一 Linux filesystem、single writer、受控 fixture、process-kill／測試注入。P0 raw 命名為 receipt 的檔案仍是 staging evidence；只有 Task `receipt_committed` event 才是本工作台的完成 authority。

## 明確未驗，不可外推

- **完整 Phase 2 failpoint matrix 未驗。** 尚未將全部 10 個 root cuts、P0 `after_intent`／`after_receipt_json`／`after_receipt` 三 hooks、所有 child/root payload cuts 串成 Phase 2 的精確 exit-97 matrix；只有代表性 `root_planned` handoff 與 after-spawn kill-parent。
- **規格 §6 的完整反例矩陣未驗。** 未逐件覆蓋所有 seven-raw missing/tamper、duplicate JSON、NaN、FIFO、symlink、oversize、錯 ID、所有 event/torn-tail 組合、known nonzero/signal/spawn error 與 raw 完整後 generation 改變。
- **golden vectors 未補。** Phase 2 process Call、manifest、evidence hash 與 receipts 尚未有 checked-in process golden vectors；既有 fake/vector tests 不可替代它。
- **不聲稱 durability 或隔離。** 不涵蓋 power loss、device cache、NFS、multiwriter、GC、portable checkpoint、actual executable snapshot、TOCTOU 安全、environment reproducibility、完整 bounded allocation 或 explicit fd-inheritance contract。
- **不聲稱產品語意。** 不涵蓋 Agent、Step/Round、正式 scheduler、queue、公平性、公開 CLI、C++ writer、common Function/Return ABI 或完整 AOS lifecycle。

## 可重查的實作入口

- [p1a2_process_runtime.py](p1a2_process_runtime.py)：root gap handoff、store layout、P0 invocation 與 recovery loop。
- [p1a2_process_binder.py](p1a2_process_binder.py)：direct-child attempts grammar、prefix、manifest 與三態。
- [test_process_after_spawn.py](test_process_after_spawn.py)：after-spawn session/process-group 證據。
- [test_process_guardrails.py](test_process_guardrails.py)：frozen recipe、generation、premature artifacts、root-planned gap。
- [test_process_phase2.py](test_process_phase2.py)：正常 commit/recovery 與端到端 corruption 零寫。
