# 全套狀態、來源與共同約束

本頁先說清楚「誰決定、證據到哪裡、哪些仍能改」。後頁不得用 schema、CLI 範例或原型檔名覆蓋這些邊界。

## 五種狀態

- **使用者已定**：產品方向與最新詞義；所有方案都要相容。
- **目前推薦**：基於安全性與現有證據的主張；仍可修訂。
- **原型暫選**：只為實驗固定；測試通過也不自動成為正式 ABI（跨語言固定介面）。
- **尚未決定**：有候選，沒有唯一答案。
- **明確延後**：不進近期實作，但仍列出限制。

## 已固定的共同模型

### 使用者已定

- **AOS** 位於 Linux 與上層工作之間。核心比喻是「Function 像 CPU 指令，filesystem 像記憶體」；硬碟是掛掉後仍在的慢速記憶體。
- Function 可由明確檔案或資料夾承載，也可呼叫其他 Function。Linux process 是通用的 leaf（組合最底層）形式：`argv + stdin -> stdout + stderr + termination`；它不是所有 Function 的定義。
- **正在執行中的 Function 實例**叫 Task；Task 不是 PID。Task 是否也包含排隊、暫停及完成後記錄，尚未由使用者固定。
- AOS 要保存狀態並管理自己的生命週期。**AOS Machine** 是以 Function 與 filesystem 為核心的基礎，不含 Agent 專屬 prompt、Step、Round 或 team。Agent 是其上層擴充，不得推回 Machine。
- Agent root 的實際絕對 path 是 Agent ID，也是它的地盤。若同 root 有多個 active Tasks，root 內必須看得見，且由 AOS 管理。
- Step 與 Round 都是 Function；Step 有原子操作角色，Round 由一連串 Step 與 Tool 呼叫構成。Tool 可沿用 process leaf。
- Agent 忙碌時有三路：普通呼叫拒絕且不建 Task；追加到目前 Task 且不另建；明確排隊才建新 Task並寫開始條件。啟動仍須防同 root 並行互踩，不能只看前 Task 狀態。
- 日常操作偏好 shell 與注入 `$PATH`；偏好 `.aos`；Agent root 應可進 Git。這些偏好沒有決定命令拼法、目錄入口或 `.aos` 精確內容。
- 可攜語意狀態和某台機器的 active set、claim、queue 必須分開；跨機後不得假裝原 process 仍連著。

### 目前推薦

- 缺少可信完整結果時標為 **unknown（結果不明）**，不盲目重送；修復或重做必須是明確操作。
- Tool 是 Function 的 Agent 角色，可為 leaf 或 composite（組合式）Function。
- 第一版每 root 只給一棵可寫 Task tree；同 root parent／child 共用資格。排隊先過條件關卡，再由中央原子取得容量／claim。
- live claim 與 active set 以中央 AOS 為真相；root 內顯示由 AOS 維護，但只是可重建的 machine-local（本機）副本，不可用來 dispatch。
- 先保存 dispatch intent（即將造成外部作用的持久意圖）再執行；已知 Return 與執行依據要能一起驗證。確切 event、Receipt 或檔案格式仍未定。

### 原型暫選

- P1 原型在接受時配置固定 Task ID，讓同一 ID 暫時貫穿 queued／running／paused／completed 記錄；每次 process 啟動只是 Task 內的一次 attempt。這是保存模型，不是使用者對 Task 的完整定義。
- P1 暫用 immutable Call、Task-local Call、parent events、Receipt 與 single writer；其 `<store>`、JSON、hash、ID 與容量都不是產品 schema。
- process 原型用 absolute executable path 加 SHA-256 表示 leaf generation；不可外推到其他 Function。
- P1a-2 已有一個真 process 接入 Receipt／composite tree 的窄切片，驗到完整 raw 重開補提交、after-spawn 不重跑與矛盾時停止。完整 Phase 2 的中斷、已知失敗與 golden 證據仍未齊；精確界線見[證據頁](../wait_user/2026-08-14-deep-dive-04-evidence.md)。

### 尚未決定

- Task 的正式起訖、ID、保留／清理、public accept 重送語意，以及 Call／Return／Receipt 是否成為正式名稱。
- process／directory generation、dependency snapshot 與 Definition／Memory version 算法。
- `.aos` exact layout、中央 store 如何引用 repo 內資料、哪些進 Git；中央狀態不可混入 portable state。
- directory 入口、common Return ABI、repair、Step 原子邊界、排隊條件與 active 顯示檔名。
- CLI、PATH sugar、JSON／event／manifest schema、Runtime 是否常駐及 store／executor 是否拆開。

### 明確延後

- 多 writer 共寫 live root、NFS、network AOS、即時跨機、安全 sandbox、secrets、大型串流與完整 GC。
- symlink、外力改名、path reuse、跨機 path 搬移；coding-agent／MCP 專屬適配。
- 私有 workspace 並行發布、完整 C++ durable writer 與 Janet 執行權，等較小 seam 通過後再做。

## 全套不可違反的規則

1. 不把 `Function == process`、`Task == PID`、`Agent == AOS Machine` 畫成等號。
2. Function 可組合；leaf process 的 stdout、stderr、termination 證據要分清，不可直接改名為 Task 成功。
3. Call、Task、Function Return、持久證據與 Tool Result 是不同角色，不得混名。
4. 未留下可信結果就是 unknown；不得把可能有副作用的舊工作自動重跑，也不得捏造成功 Receipt。
5. durable（跨重開保存）狀態先提交，才可跨入下一狀態或外部作用；I/O 失敗停在屏障前。
6. relation 由明確 authority 建立；不得掃目錄猜 root、parent、child 或可執行工作。
7. 普通 busy 拒絕、追加目前 Task、明確排隊三路不可合併；拒絕不得偷偷建 Task。
8. machine-local 執行權威與 Git 可攜狀態不可互相冒充；離線顯示必須標為過期。
9. Agent root 的 path identity 與寫入地盤規則只屬 Agent 層；Machine 仍能執行非 Agent Function。
10. 只聲稱測試覆蓋的 fault model；process-kill 不等於斷電、NFS、多 writer 或 exactly-once。
11. CLI 與 schema 在裁決或提升門檻通過前，一律標成暫選或未決。

## 來源採用與淘汰

| 來源 | 在本套的處理 |
|---|---|
| 使用者最新決定／idea ledger | **採用**為產品方向與詞義最高來源。 |
| 2026-08-14 workbench／測試 | **採用窄幅證據**；不提升其 JSON、ID 或 store 布局。 |
| yesterday AOS 草稿 | **只取核心動機**；已封存，舊 `Function = process` 與 Step 詞義淘汰。 |
| FreePy 程式／文件 | **只取實作經驗**，如 Tool 配對、單一 owner 與 pause；不是 AOS 真源。 |
| old AgentOS prototype | **淘汰為產品模型**；舊命令與 schema 只提示工程地雷。 |
| dev-guide／研究導讀 | **只取文件、Linux、持久化與安全經驗**；不得覆蓋 AOS。 |
| `langlab-janet` | **只取 embed、thread／fiber、build 經驗**；不成為依賴或架構真源。 |
| Opus 審查 | **採用已取捨的反例與建議**；「普通 busy 呼叫預設排隊」已淘汰。 |

舊文把 **Function** 限為一次 process，也沿用 FreePy 的舊 **Step／Round** 詞義；兩者都已被使用者新定義覆蓋。引用舊證據只取工程事實，不沿用舊名稱。
