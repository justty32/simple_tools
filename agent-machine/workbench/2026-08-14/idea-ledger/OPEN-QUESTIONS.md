# 尚未由使用者裁決的問題

> 這些不是缺漏或錯誤，而是目前應保留多套方案、等待原型或日後詢問使用者的地方。

## 最先要驗的基礎問題

### 1. `.aos` 到底保存什麼

- 每個 Function／agent root 內各有 `.aos/`。
- 中央 AOS store 統一保存，再由 agent root 指向它。
- 可攜語意留在 agent repo，machine-local 排程／claim 留在中央；兩邊只用明確引用連接。

使用者已表達偏好 `.aos`，但尚未選定上面哪一種邊界。

### 2. Function directory 的固定入口

- 固定檔名，例如 `./call`。
- manifest 明列入口。
- 外層 executable façade，再由內部 manifest 管理模組。

目前工作稿建議不要猜入口；但連這項限制與具體標準都尚未請使用者裁決。

四種入口與三種成長的比較見 [`MODULE-EVOLUTION-CANDIDATES.md`](MODULE-EVOLUTION-CANDIDATES.md)。

### 3. Call 與 Task 的正式生命週期

- Call 是否必須是不可修改的請求。
- Task 是否從接受起一路包含 queued、running、paused 到完成後記錄。
- 同一 Call 能否形成多個不同 Task，以及 Task ID 如何配置。
- 完成後保存多久、何時壓縮、何時可清理。

使用者只明確說「正在執行中的 Function 實例就是 Task」，其餘是目前工作模型。

四種生命週期比較見 [`TASK-LIFECYCLE-CANDIDATES.md`](TASK-LIFECYCLE-CANDIDATES.md)。

### 4. 結果不明與失敗結果

- process 已送出、但 AOS 缺少完整結果時，Task 要停在哪種狀態。
- nonzero exit、signal、launch error 是共同 Return，還是由各 Function 再解讀。
- 人工修復、明確重做與建立新 Task 的操作介面。

「不應偷偷重做可能已有副作用的工作」是目前安全暫選，尚未成為使用者親自裁決的完整規則。

## Agent 與操作體驗

### 5. 忙碌時的追加與排隊細節

普通新呼叫預設拒絕且不建立 Task，已由使用者決定。仍待原型：

- 追加 prompt 要在目前 Task 的哪個 Step／Round 安全邊界生效。
- 排隊 Task 可使用哪些開始條件，以及條件的正式寫法。
- 目前 Task 結果不明時，後續 Task 應等待、取消，還是由使用者明確放行。
- 是否允許同一 root 同時有兩個可寫 Task；若允許，要用版本檢查、私有工作區或其他方式避免互相覆蓋。
- active Tasks 要用什麼檔名、放在哪裡，以及磁碟檔案和中央 AOS 哪一邊是唯一真相。

方案比較見 [`AGENT-QUEUE-CANDIDATES.md`](AGENT-QUEUE-CANDIDATES.md)。

### 6. Shell 的最終表面

仍可比較：

```sh
aos exec ./ask "do something"
aos exec . "do something"
ask "do something"       # PATH sugar
./interact
```

PATH sugar 如何解析回明確 Function，以及 human／machine mode 是否分開，也尚未定。

### 7. Step 的「原子」邊界

- Step 內不出現 AOS 可見的 child Task，只能在 Step 前後 pause。
- Step 可含 child Tasks，但整段一次對外發布。
- 外部副作用失敗時，原子只代表觀察／排程邊界，還是還有更強要求。

### 8. Agent root 的標準布局

哪些項目只推薦、哪些必須固定仍待決定，例如 `tools/`、`subs/`、`tasks/`、`.aos/`、`interact` 與 Git ignore 範圍。

## 保存、Git 與跨機器

### 9. 穩定存檔點

- pause 是否一定代表可保存。
- checkpoint 要不要等待所有 process 停止。
- messages、Tool Results、未完成輸出、local claim 各自能否進 Git。
- clone 後預設 detached，還是可自動接上當地 AOS。

記憶視圖與安全保存條件見 [`FILESYSTEM-MEMORY-CANDIDATES.md`](FILESYSTEM-MEMORY-CANDIDATES.md)，包裝方案見 [`DURABILITY-CANDIDATES.md`](DURABILITY-CANDIDATES.md)。

### 10. Path identity 的遠期搬移

目前主線把實際絕對 path 當 agent ID。未來改名、跨機器路徑不同或 path reuse 時，要用搬移紀錄、別名、內容身分或其他方式，尚未選。

### 11. 跨機器同步方式

Git、檔案同步、中央 server、peer-to-peer AOS 各有不同衝突與安全問題。遠期要先解「可攜狀態」還是「即時排程交換」，也尚未排定。

## 實作與語言分工

### 12. Python、C++、Janet 的正式界線

- C++ 是否擁有所有 durable commit。
- Janet 只回傳決策，還是可安全地執行有限操作。
- Python 原型中的哪些行為必須由 C++ 重跑後才算證據。

責任邊界與四種接法見 [`CPP-JANET-PHASE-CANDIDATES.md`](CPP-JANET-PHASE-CANDIDATES.md)。

### 13. Tool 的資料格式

JSON envelope、raw bytes、file manifest、large output、HTTP adapter、streaming 與 secrets 的最小共同規則仍待逐步原型化。

### 14. 中央 AOS Runtime 的形狀

常駐 manager 或按需啟動、store 與 executor 是否分開、queue 公平性、AOS 自身 start／recover／drain／stop 流程，都尚未由使用者選定。

## 明確延後但不能忘記

- symlink、外力改名、path reuse。
- coding agent、MCP 的專屬適配。
- 多 writer、NFS、網路 AOS、安全 sandbox。
- secrets、權限、垃圾清理及大型輸出保存。
