# 未來文件去向

> **工作稿、非正式 AOS ABI。** 本頁規劃驗證後如何遷移，不授權現在改根層、`full/` 或 `archived/`。

## 本包的唯一真源

| 概念 | 本包真源 | 狀態 |
|---|---|---|
| 使用者方向與來源分層 | [`01-DECISIONS.md`](01-DECISIONS.md) | 使用者已定／暫選／證據／待選 |
| Call／Task／relation、registry、orphan | [`06-FUNCTION-TASK-MODEL.md`](06-FUNCTION-TASK-MODEL.md) | v2 evidence＋P1a-2 target，非正式 ABI |
| P1a-2 process binding/recovery | [`07-P1A2-PROCESS-SEAM.md`](07-P1A2-PROCESS-SEAM.md) | 尚待實作驗證的工作規格 |
| P1a-2 exact prototype formats | [`08-P1A2-FORMATS.md`](08-P1A2-FORMATS.md) | 只供本片互通，不是正式 ABI |
| 未決設計 | [`02-OPEN-OPTIONS.md`](02-OPEN-OPTIONS.md) | 待選 |
| Tool terminology | [`05-TOOL-BRIDGE.md`](05-TOOL-BRIDGE.md) | Agent bridge 工作模型 |

其他頁只摘要並連回真源，不複製 crash matrix。

## 四種去向

```text
short     穩定核心、最短操作、最重要 failure rule
full      schema 邊界、lifecycle、failure/recovery、證據契約
options   尚待驗或可替換方案
archive   被取代或已完成的研究／prototype
```

## 內容對應

| 本包內容 | short | full | options／archive |
|---|---|---|---|
| AOS proper name；Machine/Runtime/Agent | 一句話 | 三層依賴 | 舊名稱展開 archive |
| Definition／Call／Task | 使用者核心＋P1工作模型分標 | Call identity/resolver、Task生命週期方案 | CAS/ID options |
| Return 與 Receipt 分離 | known/unknown 規則 | task/call/basis binding | common Return ABI options |
| root/child 同形 | registry→root 小圖 | planned/accepted/linked/replay | public accept idempotency options |
| Task tree relation | parent events authority | ordering、projection、derived index | family stream archive/options |
| staging/orphan | planned 才 freeze | GC roots、unlinked 規則 | retention/GC implementation |
| P0 process seam | staging 非 Receipt | binder、attempt、evidence validation | full process ABI future |
| Step/Tool/Round | `Step→Tool→Step` | Agent roles/pause | visible child options |
| portability／Runtime | 通過後才入 short | checkpoint/claims/manager | P1c/P2 options |

## 建議正式閱讀結構

```text
README / short overview
  -> Function / Call / Task / Return / Receipt
  -> Runtime lifecycle + no-auto-resend
  -> Agent quick start

full/
  callable-function-and-task.md
  task-acceptance-and-roots.md
  composite-and-task-tree.md
  leaf-process-adapter.md
  runtime-and-recovery.md
  agent-step-tool-round.md
  storage-portability.md
  scheduling.md

options/
  call-resolver-and-cas.md
  task-id-and-public-accept.md
  common-return-abi.md
  repair-interface.md
  definition-entry.md
  stable-image.md
  manager-shape.md
```

## 現行文件的處理清單

- [`AOS-ARCHITECTURE.md`](../../../AOS-ARCHITECTURE.md)：移除`Function = process call`等舊等式；先保留使用者核心，再把Call identity／Receipt／registry標成P1候選。
- [`AOS-SCHEDULING.md`](../../../AOS-SCHEDULING.md)：比較accepted/eligible與agent serialization方案；不得把capacity 1寫成使用者已定。
- [`AOS-V0.md`](../../../AOS-V0.md)：採 P0、P1a、P1b、P1c、P2 milestones；P1 prototype 不取消 central Runtime 終點。
- [`AOS-INTEGRATION.md`](../../../AOS-INTEGRATION.md)：寫 agent root 的 stateful Definition/path identity/capacity；portable truth 等 P1c。
- [`AGENTLOOP-ON-AOS.md`](../../../AGENTLOOP-ON-AOS.md)：Step/Tool/Round 是 Agent roles；一次 prompt 可直接是 Round Task，不強加 Agent Task 外殼。
- [`TOOL_SPEC.md`](../../../TOOL_SPEC.md)：保留 no-shell、strict mapping、call/result pairing；對齊 [`05-TOOL-BRIDGE.md`](05-TOOL-BRIDGE.md) 的 Return/Receipt terminology。
- [`RUNTIME.md`](../../../RUNTIME.md)／[`RECOVERY.md`](../../../RECOVERY.md)：抽 durability 經驗；P0 unknown artifact 不提升為 Receipt，process kill 不提升為 power-loss guarantee。
- [`README.md`](../../../README.md)：最後更新導航，避免讀者進入半套主線。

## 提升門檻

1. P1a-1 v2只足以提升 **工作模型** 的 Task-local Call與 child relation/replay；root registry與 B1 re-stage須明標 P1a-2待驗。並保留 process-kill、full replay、`lstat`非 TOCTOU限制。
2. P1a-2先通過 B1/B2 failpoints，再接 process；完整 matrix通過後才提升 evidence→Task Receipt seam，不提升 common ABI。
3. P1b 通過後，才提升 Step/Tool/Round pause/atomic boundary。
4. P1c 通過後，才提升 agent queue、PATH UX、checkpoint/Git/clone。
5. P2 通過後，才固定 manager process、central store 與 executor 邊界。
6. 每次提升同步 archive v1 與衝突舊文，檢查相對連結、術語與單檔大小。
