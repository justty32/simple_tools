# 排程與 Agent：條件成立，不等於可以寫

這是 [`full/08–11`](../full/08-SCHEDULING-AND-EXECUTION.md) 的濃縮導讀。Agent 是 AOS Machine 上層的 stateful Function；它有自己的 root 地盤，但 root 不是 sandbox。

## Agent root 是身分，也是共享可變狀態

**使用者已定：**Agent root 的實際絕對 path 是 Agent ID 與地盤。Agent 可在裡面保存記憶、修改自己的檔案，也可決定哪些子目錄另成 child Agent。

**目前推薦：**只有經明確建立或 attach 的目錄才取得 Agent 身分；普通子目錄不因名稱或位置自動變成 Agent。第一版同一 root 只允許一棵可寫 Task tree。這不表示整台機器只能跑一件事，也不表示 tree 內只能有一個 Task ID；parent、Step、Tool child 可以同時可見，但共用一份 root 寫入資格。

## 忙碌時只有三條明確入口

這三條是使用者已定，不能為了方便合併：

1. **普通新呼叫**：root 忙就拒絕，清楚說明未收下；零 Task、零 queue、零工作目錄。
2. **明確追加**：內容屬目前 Task，不建立新 Task；先保存，等安全邊界再採用。
3. **明確排隊**：建立新 Task，並寫明目前工作到什麼狀況後才可開始競爭。

追加內容何時生效仍未決定。目前推薦第一個原型在下一個 Step 邊界採用；不在已 dispatch 的 Step 中途改 argv、prompt 或 Tool Call。

## 排隊 Task 要過兩道 gate

```text
開始條件成立
  -> 只代表 eligible（可競爭）
  -> 中央原子重驗 root 安全與容量
  -> commit claim／寫入資格
  -> durable dispatch intent
  -> executor 才能啟動
```

條件成立但 root 仍忙，Task 應顯示「等容量／寫入權」，啟動數保持 0。條件無法判定時停下來，不猜成功。

### 最重要的自鎖反例

```text
parent T1 已持有 bot-a 寫入權
T1 建立同 root child T2，並等待 T2
若 T2 再走普通 busy admission：
  T2 等 T1 釋放 root
  T1 等 T2 完成
=> 永久死鎖
```

所以 same-root child 必須繼承同一棵 tree 的資格，不重新排在 parent 後面。呼叫另一個 Agent root 則不能帶走原 root 的寫入權；對方由自己的排程器管理。

## 中央權威與 root 內顯示

**使用者已定：**同 root 若有多個 active Tasks，Agent 在地盤內必須看得見。

**目前推薦：**中央 AOS 才是 active set、claim 與容量真相；root 內檔案只是原子替換、可重建、被 Git 忽略的投影。投影缺失或過期代表「不知道」，不是 `active=0`；刪掉投影也不能釋放中央 claim。

## Step、Tool、Round 放在哪一層

**使用者已定：**Step 與 Round 都是 Function，Tool 可沿用 process leaf。以下角色切分是目前推薦，精確 Step 原子邊界仍未決定：

- **Step**：Agent 語境中的原子操作角色；目前只先保證排程與發布邊界，不保證 rollback。
- **Tool**：Function 的 Agent 角色；可是一個 leaf process，也可展開為多個 child Functions。
- **Round**：由多個 Step 與 Tool 呼叫組成的 Function。

Tool Call 經 schema／權限／binding 後才變 Function Call；輸入無效或被拒絕時仍要回配對 Tool Result，但可保持零 AOS Task。Machine 不解析模型專屬 schema。

## CLI 只是一層糖

使用者偏好 shell 與 Agent 專用 `$PATH`。目前推薦先用完全明示入口固定測試語意，再做 `cd bot-a && aos exec ./ask ...` 的日常介面；PATH wrapper 只提供薄控制入口。Foreground、detach、Ctrl-C 與重新等待都應指向同一 Task，不能因 UI 行為多做一次。

命令名稱、directory 入口、`cwd`／env、active 顯示 schema、常駐排程器與 paused 是否交出 root 都尚未決定。

完整原文：[`full/08`](../full/08-SCHEDULING-AND-EXECUTION.md)、[`09`](../full/09-AGENT-ROOT-AND-OWNERSHIP.md)、[`10`](../full/10-STEP-TOOL-ROUND.md)、[`11`](../full/11-SHELL-CLI-AND-PATH.md)。忙碌與 Step 候選見 [`options/07`](../full/options/07-AGENT-BUSY-AND-STEP-BOUNDARY.md)。
