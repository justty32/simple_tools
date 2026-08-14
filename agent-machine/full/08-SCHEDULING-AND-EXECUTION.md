# 排程與執行

排程的工作不是猜下一個 prompt，也不是解析 stdout。它只在已被接受的 Function Tasks 中，找出現在允許執行者，安全地交給合適的執行者，並保存開始與結果的邊界。

## 先分開四個問題

一個 Task 能不能跑，要依序回答：

1. **是否已被接受**：呼叫資料與關係已保存，才有可管理的工作。
2. **開始條件是否成立**：例如指定的前一個 Task 已到某個狀態。
3. **它要使用的地盤是否可用**：尤其 Agent root 不能因兩件工作同時改檔而混亂。
4. **有沒有執行資源**：queue、executor 與全機容量是否允許。

條件成立不等於已取得地盤；取得地盤也不等於已有 CPU／worker。把這些合成一個 `queued` 布林值，會看不出工作究竟在等什麼。

## Agent 忙碌時的三條入口

### 使用者已定

同一 Agent root 忙碌時：

- **普通新呼叫**：直接拒絕，零 Task、零隱藏排隊資料。
- **明確追加 prompt**：加入目前 Task，由它在合適邊界讀取；不建立另一個 Task。
- **明確建立排隊工作**：建立另一個 Task，並寫明它何時才可開始競爭執行。

這三條不能靠「反正最後都會跑」而合併。拒絕後偷偷建立 Task，或把追加 prompt 變成第二個 root writer，都違反已定行為。

追加內容何時對目前 Task 可見，以及開始條件有哪些正式種類，仍是**尚未決定**；第一版原型應只提供少量可驗證條件，不先發明通用條件語言。

## 兩道必要關卡

**目前推薦**：排隊 Task 至少通過兩道獨立關卡。

```text
開始條件成立
    ↓
取得 Agent root 的寫入資格
    ↓
才可交給 executor
```

第一道只看已保存且可信的狀態，不讀使用者隨意修改的文字檔。第二道由中央 AOS 以同一筆受保護的更新完成，不能先讀「目前空閒」再稍後寫入，否則兩個 scheduler 可能同時搶到最後一格。

### 使用者已定

若同 root 有多個 active Tasks，root 內必須看得見，而且 Agent 要有知情權。這個顯示由 AOS 管理。

### 目前推薦

中央 AOS 才是 active set 與寫入資格的真相；root 內檔案只是可重建的本機顯示。顯示過期、被刪除或 AOS 不在線時，不得據此啟動工作。即時控制命令應向 AOS 查詢；離線只能明說「上次看到的狀態」。

## 第一版的 root 寫入模型

**目前推薦**：每個 Agent root 同時只有一棵可寫 Task tree。

Round Task 呼叫 Step、Tool 或其他 child Function 時，parent 與同 root children 共用同一份寫入資格。child 不可重新排到自己 parent 後面，否則 parent 等 child、child 等 root，形成自我卡死。

一棵 tree 內可同時看見多個 Task ID，但只占一個 root writer slot。Task 數量因此不能直接拿來計算已用容量。

不同 Agent roots、無共享寫入地盤的普通 Functions，以及日後明確的唯讀 snapshot 工作，仍可並行。單 root 序列化不等於整台 AOS 只能跑一件事。

## Queue、executor 與候選

**目前推薦**：保留 queue 與 executor 的分層，但不把目前原型格式定成 ABI。

- **Queue** 表示一類工作在等待哪種執行資源與容量；只保存 Task 引用及排程必要資料，不複製整份 Function。
- **Executor** 宣告自己能處理哪些 queue、還有多少容量；leaf process executor 最終與 Linux 交接。
- **Scheduler** 從當下可執行的組合中選擇，不先挑一個 Task 再發現它沒有 executor。

一個候選至少同時含 Task、它要走的 queue、可用 executor，以及所需 root 資格。挑選後仍須重新驗證並建立唯一 claim；選擇本身不應直接改 Task 內容。

第一版可用明確 queue，不做「看起來像 GPU 工作就自動路由」的猜測。HTTP、模型 endpoint 或特殊裝置可先由本機 executable adapter 使用相應 queue；Machine 不需要新增 Agent 專屬 Task 種類。

## 排序政策可以更換

**目前推薦**：先採簡單且可解釋的順序，例如 Task priority、當前 Function priority、進入等待的時間；值相同時仍須有穩定 tie-break，不能依目錄列舉偶然順序。

日後可加入公平性、aging、deadline、rate limit 或 budget，但它們是 policy，不應改變 Call、Task 或 Return 的核心詞義。政策只能在已符合條件與容量限制的候選中選擇，不能繞過 root 寫入規則。

同一 Task tree 的 child 順序由 composite Function 的持久進度決定，不由全域 scheduler 猜控制流程。

## Claim、執行意圖與結果

**目前推薦**的安全順序是：

1. 重新驗證 Task、條件、root 與 executor 容量。
2. 中央 AOS 原子建立 claim，記下本次 Runtime 啟動代號。
3. 持久保存「即將執行」的意圖。
4. 完成耐久屏障後，才跨入 Linux process 或其他不可回收作用。
5. 收到可信完整結果，先保存證據，再讓 parent 看見 child 已完成。
6. writer 已停止或已失去發布能力後，才釋放 root 資格。

claim 是本機執行權，不是 Git 可攜狀態。AOS 重開後，舊啟動代號的 claim 不可直接復活；若舊 process 是否仍在作用無法確認，root 必須停在恢復／人工處理狀態，不能派新的 writer。

結果不明時不自動重送。非零 exit、signal 或啟動錯誤可能仍是完整、可信的 leaf Return；它們是否讓上層 Round 失敗，由呼叫者規則決定，不由 scheduler 偷判。

## Pause、stop 與執行邊界

**目前推薦**：pause 與 stop 在 Function／Step 安全邊界生效。

running leaf process 預設不作搶占式暫停；AOS 先記下要求，等待可信 Return 或 unknown，再決定下一個 child 是否建立。`SIGSTOP` 只凍結某台機器上的 process，不能變成可攜 checkpoint。

paused Task 是否保留 root 寫入資格尚未固定。若宣告已安全交出地盤，resume 就必須重新通過 root 容量與記憶版本檢查，不能無條件取回。

## AOS 自己的狀態

Scheduler 只有在 AOS 完成恢復並進入 ready 後才能派新工作。draining 時不接受新的普通工作，只讓已取得資格者到安全邊界；stopped 時沒有活 claim。

資料夾顯示至少要讓 Agent 分得出：目前 writer tree、其他 active Tasks、已符合條件但仍等容量者、仍等條件者，以及 AOS 是 ready、recovering 還是只剩離線快照。精確檔名與欄位仍未決定。

## 後續並行方案

**明確延後**：多人直接共寫 live root。較安全的成長順序是先加入唯讀 snapshot，再試每 Task 私有 workspace、單一發布者與版本衝突檢查。這些方案詳見 options；第一版不因未來並行而放棄單 writer 的可驗證基準。
