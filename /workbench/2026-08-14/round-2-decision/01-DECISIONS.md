# 已決事項、P1 暫選與證據邊界

> **工作稿、非正式 AOS ABI。** 標記定義見 [`README.md`](README.md)。細部 schema 以 [`06-FUNCTION-TASK-MODEL.md`](06-FUNCTION-TASK-MODEL.md) 為本包唯一真源，process seam 以 [`07-P1A2-PROCESS-SEAM.md`](07-P1A2-PROCESS-SEAM.md) 為真源。

## 1. 使用者已定

### 三層與 Function

- `AOS` 是 proper name。AOS Machine 以 Function 作 CPU instruction、filesystem 作 memory；Runtime 管中央排程、durability/recovery 與自身 lifecycle；Agent 在上層賦予 Step、Tool、Round、messages、LLM 與 team 語意。
- Step 與 Round 都是 Function。Step 具有原子操作角色；Round 是由一連串 Step 與 Tool Calls 組成的 Function。
- Function Call 被 AOS 接受後，正在執行與已留下記錄的實例都是同一個 **Task**。Task 不是 PID；Functions 可呼叫 Functions，因此 Tasks 可成 tree。
- Linux process `argv + stdin -> stdout + stderr + termination` 是 leaf 的底層 adapter，不是所有 Function 的定義。

### Agent root 與入口

- agent root 是特殊 stateful directory：絕對純 path 同時作 agent identity 與 serialization key，預設 capacity `1` 覆蓋活躍 Task tree。只有明確建立的 descendant 才是 child-agent root。
- `aos exec TARGET` 是通用 Function→Task primitive；Machine 只接受明確 filesystem path，不搜尋 PATH。候選使用方式是：

```sh
cd bot-a
aos exec ./ask "do something"
```

- callable 可從 executable leaf 長成 directory module；directory entry 必須明示，不能猜 `main.py`。

## 2. 內部+Opus 暫選

### Call、Task、relation

- Call 是純請求，只含已解析 Definition、argv、stdin bytes、cwd、environment policy與其他 policy；不得含 `task_id`、`parent_id`、slot、state。
- `call_ref{hash,size}` 是 Call 唯一 identity。每個 Task 在 P1 自存完整且在 planned 後 immutable 的 `call.json`；這是 P1 resolver/location policy，不排除日後改 CAS。
- `task.json` 只把 `task_id` 綁到 `call_ref`。同一 Call 可以產生不同 Tasks；P1a-1 v2 的 first／second 即是實證。
- relation 唯一由 upstream ordered events 保存。child 不存 authoritative backlink，也不掃 `tasks/` 猜 tree。root 使用 store-level root registry，registry 扮演其 upstream parent。

### Return、Receipt 與 unknown

- **Function Return** 是 caller-facing semantic result；**Task Receipt** 是某 Task 已知完成的 durable record，綁 `task_id + call_hash + basis`。leaf basis直接含 attempt/P0 invocation/evidence hash；composite basis是帶 slot的 ordered child Receipts。
- P1 為簡化把 Return 內嵌在 Receipt；兩者概念仍分開。P0 名為 receipt 的 artifact 只是 process staging evidence。
- unknown 沒有 Task Receipt。dispatch intent 後缺完整可信 evidence 時進 `repair_required`、不自動重送；parent 不產生 Receipt，後續 child 不 dispatch。

### 發布與 root 入口

- `call.json` 在 upstream `*_planned` commit 前只是可替換 staging；definition 改變可重建，未被引用者是可丟 orphan。planned commit 才 freeze bytes/hash。
- root只由內部 `accept_fixture_root(root_id,resolver)`顯式 stage/plan；純 `recover_store()`不接受也不 resolve。pre-planned crash後 recover見0 roots，只有顯式同-ID re-accept可換 staging。planned後 recover才補 task/accepted/linked。這不承諾 public idempotency/request ID。
- child可由 accepted parent的 stable slot re-stage；root與 child的 pre-planned規則不可混用。root/child planned及 linked都綁完整 `call_ref{hash,size}`。
- P1a-2 planned前把 executable解成 absolute path並算 content SHA-256 generation；只在 Task `dispatch_intent`前驗。intent後 Definition改變不推翻完整舊 evidence。
- Task attempt authority只有 `events.jsonl`；Receipt payload在 `payload/<hash>.receipt.json`，唯一 semantic completion是 `receipt_committed{hash,size}` event。

完整協定見 [`06-FUNCTION-TASK-MODEL.md`](06-FUNCTION-TASK-MODEL.md)。

## 3. P1 驗證證據

- P1a-1 v1 已被 v2 取代。現有 v2 code/tests有 **18 個 test methods／23 個 failpoints**；matrix在 WSL `/tmp` 以 process kill後十次獨立 replay檢查 idempotence。
- 已驗不變量包括：相同 Call bytes/hash可有不同 Task IDs；relation不滲入 Call；parent ordered planned/linked/observed可向下 replay；unlinked call-only directory不進 tree/不 dispatch；known result recovery不增加 dispatch count。
- **未驗：**v2 root仍由固定 ID的 `initialize()`建立，沒有 durable registry；pre-planned staged Call若 bytes不同會報 corruption，沒有 definition改變後 re-stage。因此 B1/B2是 Opus准入條件與 P1a-2 phase 0；B4只有「unlinked不 dispatch」有測試，GC候選規則仍只是規格。
- 證據只涵蓋 process-kill/failpoint 與同一 Linux filesystem。它不涵蓋 power loss、device cache、NFS、多 writer、external exactly-once 或 side-effect rollback。
- residual：validator/progress 每次 full replay 的成本不可擴充；`lstat` 只擋明顯 symlink，並非 fd-based TOCTOU 防護；derived wakeup index、migration 與 GC 尚未做。

證據與裁決：[`v2 README`](../p1a-task-tree-python-v2/README.md)、[`v2 NOTES`](../p1a-task-tree-python-v2/NOTES.md)、[`Opus review`](../opus-p1a-review/OPUS-REVIEW.md)。README/NOTES是 review 前的當時記錄，仍寫「等待 Opus」。

## 4. 待選

- Step 是否可含 AOS-visible children、完整原子排程邊界與 pause/interleave 規則：P1b。
- common composite Return ABI與 known nonzero/signal/spawn_error的 parent mapping、unknown lifecycle及 repair UI。這三種 termination本身都是完整已知 leaf Returns，不是 unknown。
- task-local resolver 是否長期改 content-addressed store、Task ID allocation、GC／retention。
- directory module entry、PATH sugar、agent queue、checkpoint／Git／clone：P1c。
- central Runtime 的 manager/store/executor 形狀：P2。中央 Runtime 方向本身不是待選。

真正仍開放的方案只放在 [`02-OPEN-OPTIONS.md`](02-OPEN-OPTIONS.md)，不得把 prototype evidence 提升成正式 AOS ABI。
