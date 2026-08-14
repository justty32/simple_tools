# 使用者方向、P1 暫選與證據邊界

> **工作稿、非正式 AOS ABI。** 細部行為以 [`06`](06-FUNCTION-TASK-MODEL.md)、[`07`](07-P1A2-PROCESS-SEAM.md) 為準；prototype 格式以 [`08`](08-P1A2-FORMATS.md) 為準。

## 1. 使用者已定

### AOS 主軸

- `AOS` 是名稱。核心抽象是「Function 像 CPU 指令，filesystem 像記憶體」。
- AOS 位在 Linux 與上層工作之間，讓 Task 盡量不直接承受 PID、process 消失等細節。
- AOS 除了排程別人，也必須保存狀態並考量自己的生命週期；硬碟是 AOS 掛掉後仍存在的慢速記憶體。
- AOS Machine 是基礎；Agent 是建立在它上面的重要擴充。Machine／Runtime／Agent 是目前建議的整理方式，不把精確三層邊界冒充使用者原話。

### Function、Task 與 Agent

- 檔案或資料夾路徑可承載 Function Definition；簡單項目可由單檔長成 directory module。
- Linux process `argv + stdin -> stdout + stderr + termination` 是 leaf 底層形式，不是所有 Function 的定義。
- 正在執行中的 Function 實例叫 **Task**；Task 不是 PID。Function 能呼叫 Function，因此大型工作能由較小工作組成。
- Step 與 Round 都是 Function。Step 有原子操作角色；Round 是一連串 Step 與 Tool Calls 組成的 Function。精確原子邊界仍待選。
- agent 的絕對 root path 是其 ID 與地盤。它可同時承載 Function Definition；對 stateful agent，也是自身記憶、執行影響與管理範圍。
- 使用者偏好避免同一 agent 同時有多個會改寫地盤的實例；忙碌時排隊、拒絕或容許哪些唯讀操作尚未決定。

### 操作、保存與演進

- 使用者提出候選形式：`cd bot-a && aos exec ./ask "do something"`，也偏好 shell 內以 `$PATH` 注入 agent 操作；最終 surface 尚未固定。
- agent root 應可進 Git；machine-local 排程狀態與可攜狀態要小心分開。
- 動態 Task 落盤要謹慎；pause 等穩定狀態較適合形成可攜 checkpoint。這不排斥中央 Runtime 在執行中寫 machine-local crash journal。
- 使用者偏好 `.aos`，仍要求比較其他放置方案。跨機 Git 狀態與兩台 AOS 網路同步是不同且較後面的問題。

## 2. P1 暫選

以下只為本輪可證偽原型固定：

- Call 是純請求；planned 後 bytes 不可修改。AOS accepted 後建立 durable Task record，完成後仍保留該 record。
- `call_ref{sha256,size}` 是 P1 identity；每 Task 暫存完整 `call.json`。同一 Call可建立不同 Task IDs。
- parent ordered events保存 child relation；top-level Task由 store-level registry引用。`root_id` 等於 top-level Task ID，不是 agent path ID。
- known Return以 Task Receipt綁 Task、Call與執行依據；incomplete unknown無 Receipt且不自動重送原 Task。
- `events.jsonl` 是 P1 Task attempt/state authority；Receipt payload只有經 `receipt_committed` event才算完成。
- `first` process leaf暫用 absolute executable path＋content SHA-256 generation。這不是 directory／composite／agent Definition generation規則。
- P1a-2 暫採 single writer、store exclusive lock、machine-local `<store>` 與本包 exact formats。

這些選擇即使 prototype 通過，也只能成為推薦依據；正式 AOS 文件仍須保留來源標記與可替換方案。

## 3. 候選／待選

- `aos exec TARGET` 是否成為正式 primitive、CLI 是否搜尋 PATH，以及 PATH sugar如何降低成明確 target。
- directory Function採固定入口、manifest或 executable façade；「不猜 `main.py`」只是目前安全建議。
- Task 是否正式從 accepted 一路涵蓋 queued/running/paused/completed，以及 retention／GC。
- agent root的 capacity、serialization範圍、queue/reject/read-only policy。
- `.aos` 位於各 root、中央 store或分離 portable/local兩邊。
- common Return ABI、unknown／repair介面、public accept idempotency與 Task ID配置。
- AOS Runtime是常駐或按需、store/executor是否分離，以及完整 start/recover/ready/drain/stop流程。

完整方案見 [`02-OPEN-OPTIONS.md`](02-OPEN-OPTIONS.md)。

## 4. 證據邊界

- P1a-1 v2 支持 Task-local Call、相同 Call bytes可配不同 IDs、parent relation/replay、unlinked 不 dispatch與 known result不重做。
- 它未驗 root registry、root child recipe、pre-plan re-stage、single-writer lock、strict process binder或 P1a-2 exact schema。
- 既有 fault model只有 WSL `/tmp` 的 process kill/replay；不涵蓋 power loss、device cache、NFS、多 writer、TOCTOU、side-effect rollback或 exactly-once。
- P1a-2通過也不驗 agent root、Step/Round、portable checkpoint、完整 AOS lifecycle或 C++ writer。
