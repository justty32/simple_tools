# Scheduler 的形狀選項

狀態：**尚未決定**。主線已要求中央 AOS 對 active set、root 寫入資格與 claim 負責，但沒有決定它是一個常駐 daemon、按需 process，或如何拆 executor。P1 已有第一個真 process 接入 Receipt／composite tree 的窄切片，仍沒有 queue、claim 競爭、跨 root 容量或真 scheduler 證據；詳見[證據頁](../../wait_user/2026-08-14-deep-dive-04-evidence.md)。

不論採哪案，都要保留[排程主線](../08-SCHEDULING-AND-EXECUTION.md)的兩道關卡：開始條件成立，只表示 Task 可以競爭；中央原子取得 root／資源資格後，才可持久寫入 dispatch intent 並造成外部作用。

## 方案一：按需單一 process

每次命令取得 store 排他權，在同一 event loop 內恢復、挑選、claim 與執行；沒有背景工作時 process 結束。

- **優點**：元件少；容易插入 crash point；第一個語意原型最好除錯。
- **缺點**：無人操作時不會自動前進；長 executor 會拖住管理流程；多 client 的交接與喚醒仍要另做。

適合建立 reference behavior，不代表最終部署。

## 方案二：每位使用者一個中央 Runtime

一個 Runtime 擁有 store 與 scheduler；executor 透過受控介面領 claim、回完整結果。第一版 executor 可以仍在同一程式內，之後才拆 process。

- **優點**：active set、容量與 recovery 有單一權威；背景 queue 能持續工作；較容易拒絕舊啟動世代的回覆。
- **缺點**：要處理 daemon 啟停、升級與故障隔離；Runtime 本身卡住會影響多個 roots；介面若過早固定會形成 ABI 債。

這是正式 Runtime 的主要候選，但不是已實作事實。

## 方案三：每個 Agent 一個 worker，中央只仲裁 claim

各 root 有自己的長壽 worker；中央服務只管理跨 root 容量、唯一 claim 與啟動世代。

- **優點**：一個 Agent 故障較不會拖住其他 Agent；root-local policy 容易客製。
- **缺點**：中央與 worker 形成分散式協定；重開時要處理兩邊世代與失聯；parent 呼叫另一 root 時更容易出現等待環。

只有在單一 Runtime 的隔離或擴充真的成為瓶頸時才值得。

## 方案四：filesystem queue 加合作式 workers

沒有常駐中央 owner；多個 worker 讀檔並用 lock／atomic create 競爭工作。

- **優點**：可用普通 Unix 工具觀察；部署看似簡單；worker 可隨用隨開。
- **缺點**：掃目錄不能取代 relation authority；多檔狀態與 capacity race 很難說清；舊 process 與新世代可能同時發布。

若沒有單一可驗證的 claim authority，本案違反主線，不建議第一版採用。

## 目前推薦

先用**方案一實作方案二的語意**：單一中央 authority、單一 event loop、明確 executor seam，但最初可以是一個 process 和普通檔案 store。語意與 crash corpus 穩定後，再決定是否常駐、是否把 executor 拆出；不要先凍結 socket、queue JSON 或 SQLite schema。

parent 與同 root children 共用一份 writer 資格；Task ID 數量不等於已用 slot。不同 roots 或不共享地盤的 Functions 可並行，所以這個基準不是全機單工。

## 驗證與推翻條件

1. 兩個 scheduler 同時搶最後一個 root slot，恰好一個 claim 成功；另一個保持 eligible／等容量。
2. 高優先 Task 的 executor 忙碌時，不得擋住其他 queue 中可執行的候選。
3. parent 持有 root 資格並呼叫同 root child 時，child 不重新等待自己；active Tasks 可多個但 writer slot 仍為一。
4. claim 後、intent 前與 intent 後各殺一次 Runtime；前者 effect 為零，後者缺完整結果時停在 unknown。
5. Runtime restart 尚在 recovering 時零 dispatch；舊 startup token 的 result／publish 一律拒絕。
6. 降低容量時不強殺既有 Task，只停止新 claim；root 投影被刪除也不改中央結果。

如果單一 event loop 無法同時 drain leaf streams 與保持控制面可回應，就拆 executor，不必推翻中央 authority。只有量測顯示中央 Runtime 成為不可接受的故障範圍，且方案三能通過同一競爭與恢復測試，才改推薦。原始候選脈絡見[Agent queue](../../workbench/2026-08-14/idea-ledger/AGENT-QUEUE-CANDIDATES.md)。
