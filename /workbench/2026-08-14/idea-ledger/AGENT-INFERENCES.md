# Agent 的推論、暫選與原型所得

> 本頁不是使用者要求。內容來自主 agent、子 agent、Opus 或 prototype；可被新證據與使用者裁決推翻。

## 1. 目前採用的白話解讀

### AOS 三層

目前把整體分成：

- **AOS Machine**：Function 與 filesystem 的基礎抽象。
- **AOS Runtime**：接受工作、排程、落盤、重啟恢復，並管理自己的生命週期。
- **AOS Agent**：在上面加入 messages、Step、Tool、Round、LLM 與 team。

這個三層命名方便討論，但正式邊界仍可調整。

### Definition、Call、Task

目前工作模型是：

```text
Definition + Call --被 AOS 接受--> Task
```

- Definition 由明確 filesystem path 承載；leaf 最後接 Linux process，較大的 Function 可組合其他 Functions。
- Call 是一次純請求；暫選為不可修改，且不放 Task ID、parent 或執行狀態。
- Task 從 Call 被接受時開始，不等於 PID；暫時也把 queued、running、paused 與完成後記錄視為同一 Task 的不同階段。
- 同一 Call 可以被接受多次，形成不同 Tasks。

其中只有「執行中的 Function 實例叫 Task」直接來自使用者；接受前後的完整生命週期是 agent 延伸。

## 2. Return、Receipt 與結果不明

目前把兩件事分開：

- **Function Return**：Function 對 caller 回傳的結果。
- **Task Receipt**：AOS 對「哪個 Task 基於哪些執行證據得到哪個 Return」留下的可信記錄。

暫選規則是：缺少完整可信證據時，不製造 Receipt，也不自動重送可能已有副作用的工作；先標成需要人工處理。這是安全推論，不是 external exactly-once 保證。

P0 裡名為 `receipt.json` 的檔案目前只被視為 process evidence，不等於 AOS Task Receipt。

## 3. Call 保存與 Task tree

Opus 與 P1a-1 v2 暫時支持：

- `call_ref{hash,size}` 作 Call bytes 的身分；`call.json` 放在哪裡只是保存政策。
- 每個 Task 自存完整 Call，避免 child 的意義依賴 parent 保存多久。
- parent 的有序事件是 child relation 的唯一依據；child 不保存第二份權威 parent backlink。
- root 未來由 store-level registry 作入口，不掃 `tasks/` 猜 root。

P1a-1 v2 的 18 tests／23 failpoints只驗了 Task-local Call、child relation 與 replay；尚未證明 durable root registry 或 Definition 改變後的重新 stage。

## 4. Planned、orphan 與 Definition generation

目前的 P1a-2 目標是：

- `call.json` 在 upstream planned commit 前只是可替換 staging。
- planned 後才凍結 Call bytes；planned-but-unlinked 不可當垃圾刪除。
- 沒有任何 planned reference 的 staged directory 才是 orphan 候選。
- process leaf 接受前把 executable 解成 absolute path，並以入口檔 content hash 暫作 generation。
- dispatch 前 generation 不符便停止，不偷偷改用新版本。

這些是 Opus 的准入條件與待驗規格，不是使用者親自指定的 schema。

## 5. Agent root 的目前解讀

依使用者方向，agent root 的 absolute path 暫同時作：

- 對外 agent identity；
- 同一 stateful agent 的排他鍵；
- 預設執行與影響自身的工作目錄。

因此暫選預設 capacity `1`，而非宣稱所有一般 Function 都不可並行。repo 內保存可攜 code/config/memory；machine-local claim 與 attachment 留在中央 AOS。這個 Git 邊界仍須 P1c 原型。

## 6. Prototype 路線的暫時切法

目前不是直接做完整 AgentOS，而是逐片驗證：

1. P0：一個 Linux process 呼叫及 raw bytes／termination。
2. P1a：Call、Task、relation、root recovery 與 process evidence 接縫。
3. P1b：Step、Tool、Round 與 pause 邊界。
4. P1c：agent root、`.aos`、queue、Git／checkpoint／clone。
5. P2：中央 Runtime manager/store/executor。

每片只可提升實際通過的窄結論。Python 是快速模型；C++ 才驗 Linux process 與較硬的邊界；Janet 暫作可替換 policy，不直接提交正式狀態。

## 7. 尚不可寫成正式承諾

- process-kill 測試不等於拔電、NFS 或跨機 durability。
- `fsync`、rename 與 hash 不等於安全 sandbox 或 external exactly-once。
- P1 的 full replay、固定 Task ID 與 JSON 拼法不是正式 ABI。
- `.aos` 尚未有正式布局。
- root registry、stable checkpoint、Git clone/attach 與 network AOS 都尚未實證。
- AOS 是否永遠不重試、Step 是否可含可見 children、nonzero/signal 如何形成共同 Return，都還沒定案。
