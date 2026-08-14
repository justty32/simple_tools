# AOS 下一輪決策包

> **工作稿、非正式 AOS ABI。** 本目錄整理 2026-08-14 的使用者方向、P1 暫選、既有證據與 P1a-2 工作規格；不取代根目錄正式文件。

## 結論來源標記

- **使用者已定**：使用者親自說過或接受的產品方向。
- **P1 暫選**：只為本輪原型固定，不能直接升成正式 AOS。
- **P1 驗證證據**：prototype 實際支持的窄結論。
- **待選**：仍要比較方案、做原型或詢問使用者。

## 先讀哪裡

1. [`01-DECISIONS.md`](01-DECISIONS.md)：把使用者方向、P1 暫選與待選分開。
2. [`06-FUNCTION-TASK-MODEL.md`](06-FUNCTION-TASK-MODEL.md)：root acceptance、Task relation、恢復與持久化次序。
3. [`07-P1A2-PROCESS-SEAM.md`](07-P1A2-PROCESS-SEAM.md)：只把 `first` 換成真實 process leaf。
4. [`08-P1A2-FORMATS.md`](08-P1A2-FORMATS.md)：本原型專用的精確格式與大小上限。
5. [`09-P1A2-COMPOSITE-FAKE.md`](09-P1A2-COMPOSITE-FAKE.md)：Phase 1 與 process 之間的兩個 fake child 串接工作台。
6. [`10-P1A2-COMPOSITE-FAKE-FORMATS.md`](10-P1A2-COMPOSITE-FAKE-FORMATS.md)：fake 串接的 exact 工作台格式、容量與 golden gate。
7. [`02-OPEN-OPTIONS.md`](02-OPEN-OPTIONS.md)：正式 AOS 仍未決定的方案。
8. [`03-PROTOTYPES.md`](03-PROTOTYPES.md)：P0 → P1a → P1b／P1c → P2 路線。
9. [`04-DOC-MAP.md`](04-DOC-MAP.md)：未來如何遷移到正式文件。
10. [`05-TOOL-BRIDGE.md`](05-TOOL-BRIDGE.md)：Agent Tool 與 AOS Function 的橋接。

## 名稱先切清楚

- 本包的 **root** 是 Task tree 最上層 Task；`root_id == 該 Task ID`。
- 它不是 agent root，也不是 `/home/mine/agents/bot-a` 這種 agent 絕對路徑 ID。
- `sequence-two` 只是一般 composite Function fixture，不是 Step 或 Round。
- `<store>` 是 machine-local crash workbench，不是正式 `.aos`、Git Task image 或 agent repo 布局。
- `recover_store()` 只驗恢復入口，不等於完整 AOS 啟動、排程、排空與停止生命週期。

## 目前狀態

| 切片 | 狀態 | 只能支持什麼 |
|---|---|---|
| P0 Python | 已完成工作台 | process bytes／termination／staging；不是 Task Receipt |
| P1a-1 v1 | 已被取代 | 不再作提升依據 |
| P1a-1 v2 | 已驗 baseline | Task-local Call、parent relation/replay、unlinked 不 dispatch |
| P1a-2 Phase 1 | 已通過獨立 audit | 僅 root registry／accept／recover；47 tests × 4 runs = 188 executions |
| P1a-2 Phase 2 | 待驗候選 | `first` process leaf、strict binder、Receipt commit、crash matrix |
| P1a-2A composite fake | 待驗候選 | 從 root registry 長出兩個 fake child；不改 `sequence_two` |

既有v2有18個test methods／23個列舉failpoints。P1a-2 Phase 1 已通過獨立 audit：47 tests各跑4次，共188 executions，窄幅證明root registry／accept／recover工作台；它尚未進入 child relation 或 process execution。這不是正式AOS已實作。Fault model仍只是同一Linux filesystem的process kill/replay，不是power loss。

## P1 工作模型

```text
Function Definition + P1 immutable Call --accepted--> P1 Task record
known Return + execution basis          --commit--> Task Receipt
incomplete/unknown                         --X--> no Receipt
```

這只是 P1 用來做實驗的模型。使用者只明確說「正在執行中的 Function 實例叫 Task」；Call 是否必須 immutable、Task 是否從 accepted 延續到完成後記錄、同一 Call 是否能形成多個 Tasks，以及 Receipt 是否成為正式名詞，都尚未由使用者固定。

P1 暫用 `call_ref{sha256,size}`、Task-local `call.json`、ordered relation events與 content-addressed Receipt。精確 schema只見 [`08`](08-P1A2-FORMATS.md)，不可外推成 composite、directory、agent Function 的共同 ABI。

## 證據與限制

- P1a-1 v2：[`README`](../p1a-task-tree-python-v2/README.md)、[`NOTES`](../p1a-task-tree-python-v2/NOTES.md)。
- Opus 審查：[`OPUS-REVIEW.md`](../opus-p1a-review/OPUS-REVIEW.md)。
- P0 process staging：[`README`](../p0-function-python/README.md)、[`NOTES`](../p0-function-python/NOTES.md)。

P1a-2 固定 single writer、舊 worker 已停止、同一 Linux filesystem、bounded files。它不證明 power-loss durability、TOCTOU 安全、多 writer、NFS、environment reproducibility、portable checkpoint、common Return ABI 或完整 AOS lifecycle。
