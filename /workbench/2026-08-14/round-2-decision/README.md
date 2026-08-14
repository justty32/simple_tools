# AOS 下一輪決策包

> **工作稿、非正式 AOS ABI。** 本目錄整理 2026-08-14 的裁決、P1a-1 v2 證據與 P1a-2 目標規格；不取代根目錄正式文件。

## 結論來源標記

- **使用者已定**：產品／概念方向，不由 prototype 推翻。
- **內部+Opus 暫選**：本輪採用、仍須正式化的設計。
- **P1 驗證證據**：prototype 能支持的窄結論，不外推成通用 ABI。
- **待選**：仍須原型或使用者裁決。

## 先讀哪裡

1. [`01-DECISIONS.md`](01-DECISIONS.md)：裁決、暫選、證據與未決項分層。
2. [`06-FUNCTION-TASK-MODEL.md`](06-FUNCTION-TASK-MODEL.md)：Call／Task／relation、root registry、staging 與 orphan 的 P1 工作契約。
3. [`07-P1A2-PROCESS-SEAM.md`](07-P1A2-PROCESS-SEAM.md)：P1a-2 唯一 implementation seam 與 crash/falsification matrix。
4. [`02-OPEN-OPTIONS.md`](02-OPEN-OPTIONS.md)：真正仍開放的方案。
5. [`03-PROTOTYPES.md`](03-PROTOTYPES.md)：P0 → P1a → P1b／P1c → P2 路線。
6. [`04-DOC-MAP.md`](04-DOC-MAP.md)：驗證結果往正式文件遷移的方式。
7. [`05-TOOL-BRIDGE.md`](05-TOOL-BRIDGE.md)：Tool Call、Tool Function Call、Task Receipt 與 Tool Result 的橋接。

## 目前狀態

| 切片 | 狀態 | 能支持的結論 |
|---|---|---|
| P0 Python | 已完成工作台 | process bytes／termination／staging；不是 Task Receipt |
| P1a-1 v1 | 已被取代 | 不再作閱讀或提升依據 |
| P1a-1 v2 | 已驗 child/tree baseline | Task-local Call、Task/Call 分離、parent relation/replay、unlinked orphan不 dispatch |
| P1a-2 phase 0 | Opus 准入條件 | 先實作 B1 definition re-stage、B2 durable root registry；B4 固定規格規則 |
| P1a-2 process seam | 本輪工作規格 | 再只把 `first` fake leaf 換成 deterministic P0 process；`second` 保持 fake |

**P1 驗證證據：**現有 v2 code/tests 有 18 個 test methods與 23 個 failpoints，涵蓋同一 Linux filesystem 的 process-kill/replay matrix，不是 power loss。它支持 Task-local Call、parent child relation/replay與 known result不重跑；沒有 `roots.jsonl/root_planned/root_linked`，也沒有 definition改變後的 pre-planned re-stage，故 B1/B2仍未驗。

## 一句話模型

```text
Function Definition + immutable Call --accepted--> Task
known Return + execution basis            --commit--> Task Receipt
unknown                                      --X--> no Receipt
```

Call只描述請求；`call_ref{hash,size}`是 identity，同一 Call可形成不同 Tasks。Task `events.jsonl`是 attempt/state authority；content-addressed Receipt只有經 `receipt_committed` event才完成。child relation由 parent events決定；root registry是 P1a-2待驗 authority，accept與 recover入口明確分開。

## 證據鏈

- P1a-1 v2：[`README.md`](../p1a-task-tree-python-v2/README.md)、[`NOTES.md`](../p1a-task-tree-python-v2/NOTES.md)。兩者是當時的 prototype 記錄，仍寫「等待 Opus」，不是 v2.1 release note。
- Opus 裁決：[`OPUS-REVIEW.md`](../opus-p1a-review/OPUS-REVIEW.md)。
- P0 process staging：[`README.md`](../p0-function-python/README.md)、[`NOTES.md`](../p0-function-python/NOTES.md)。

殘餘限制包括每次 transition 的 full replay 成本、`lstat` 檢查不防 TOCTOU、單 writer、固定 fixture 與未驗 common Return ABI。詳見 [`06-FUNCTION-TASK-MODEL.md`](06-FUNCTION-TASK-MODEL.md)。
