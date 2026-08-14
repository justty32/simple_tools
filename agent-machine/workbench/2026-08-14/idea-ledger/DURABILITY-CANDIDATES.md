# 持久保存候選

> 本頁整理 `durability_round2` 支線的想法。這是後續原型的比較表，不是 `.aos` 正式布局。

## 先把兩種資料分開

建議不要把 AOS 正在運作的狀態，直接當成可進 Git 的 Task image。

| 中央 AOS 的即時狀態 | `.aos/` 可攜 image |
|---|---|
| 佇列、工作領取記錄（claim）、worker、目前執行次數（attempt） | Call、messages、Tool Results |
| AOS 啟動世代、排程優先級 | 已完成 Receipt、Task 關係 |
| PID、FD、socket、lock | pause 時的安全續接位置 |
| 部分輸出、暫存檔、本機路徑 | Definition 路徑與內容版本 |
| 正在執行或結果不明的效果 | 明確的 blocked／待人工處理原因 |

共同底線候選：

- `.aos` 保存可重建的邏輯狀態，不是 Python／Janet heap dump。
- PID、FD、socket、thread、lock 與 worker claim 不可攜。
- `pause requested` 不等於安全；一般 Linux process 不能假裝搬到另一台機器繼續。
- checkpoint 一旦完成就不再修改；新的保存點建立新版本。
- attach 後先保持 paused，不自動執行；另下 resume 才開始。
- 已送出但證據不完整的副作用標成 unknown，不因 restart／attach 偷偷重跑。
- unknown 可做診斷備份，但不能假裝成可自動續跑的 checkpoint。

## 方案 A：每個 agent root 自帶全部狀態

```text
bot-a/.aos/
  portable/       # 可進 Git
  local/          # 不進 Git
  events.jsonl
```

中央 AOS 只保存路徑索引與全域鎖；每個 agent 自己記錄即時狀態與 checkpoint。

優點：

- 最貼近「資料夾就是 agent 地盤」。
- 容易閱讀、Git diff 與手動除錯。
- Python 小原型最好做。

缺點：

- 即時資料與可攜資料太接近，容易誤 copy 或誤 commit。
- agent 多時，中央排程要掃很多目錄。
- 跨 agent 的 queue、claim 與一致更新很難處理。

適合：單一 agent 與早期實驗，不建議直接當最終中央 Runtime。

## 方案 B：中央執行庫加 `.aos` checkpoint

```text
中央 AOS store
  SQLite metadata
  artifacts/

bot-a/.aos/
  checkpoints/<checkpoint-id>/
  HEAD
  provenance/
```

中央 store 是即時排程的唯一依據；agent repo 的 `.aos` 只放已封存的可攜 checkpoint。

優點：

- 可攜資料與即時排程責任最清楚。
- Git 不會帶走「原機仍在執行」的假象。
- SQLite 適合 queue、Task、attempt、claim 與查詢。
- 能逐步長成中央 AOS Runtime。

缺點：

- 使用者要理解「中央目前狀態」可能比上次 checkpoint 新。
- 中央 store 與 repo 之間需要固定提交順序。
- SQLite 與大型輸出檔要一起備份與驗證。

適合：第一套正式實作候選，也是本輪推薦。

實作可先讓 SQLite 保存小型狀態，大型 stdout／Receipt／artifact 用普通檔案；必須先完整發布檔案，才在資料庫提交引用。

## 方案 C：不可修改的內容物件庫

每個 Call、Receipt、checkpoint 與 event segment 依內容 hash 保存。`.aos` manifest 列出 image 需要哪些物件；attach 建立新分支，不改舊歷史。

優點：

- audit、fork、去重與未來網路同步最好。
- checkpoint 天生不可修改。
- 很容易比較兩個 image 的共同歷史。

缺點：

- 垃圾清理、缺件、權限與格式升級都更複雜。
- 人類直接閱讀不如普通資料夾。
- 現在使用會把 AOS 原型變成物件庫專案。

適合：網路 AOS 前的遠期演進，不適合目前直接採用。

## `.aos` 包裝也有多種方式

| 方式 | 優點 | 問題 |
|---|---|---|
| `.aos/` 資料夾 | Git、閱讀與模組成長最好；推薦 | 檔案多，要有完整性檢查 |
| 單一 `.aos` SQLite 檔 | copy 看起來簡單 | diff／merge 差；封存須用 SQLite backup API |
| 小 manifest 指向中央 store | repo 很小 | 不能單獨跨機，不符合可攜目標 |
| manifest 加打包物件 | 適合方案 C | 現階段太重 |

保存點也建議分兩種名稱：

- `checkpoint`：沒有 running attempt、沒有 unresolved unknown；attach 後可由人明確 resume。
- `diagnostic snapshot`：可保存 unknown 與部分證據；attach 後只能 blocked／inspect。

## 建議的搬移順序

```text
pause request
  -> 等目前 Function 到安全邊界
  -> paused-safe
  -> 建立 staging image
  -> 驗證、fsync、atomic rename
  -> 中央記錄 checkpoint committed
  -> 更新 .aos/HEAD
  -> detach 並釋放本機排程權
  -> Git commit／push／clone
  -> attach，建立新的本機連接
  -> 重驗 Definition、cwd、規則與必要檔案
  -> 保持 paused
  -> 使用者明確 resume
```

離線狀態下，同一份 image 被兩台機器同時 attach，沒有中央協調者就無法完全防止。安全預設可用 `attach --fork`；要延續原 Task，使用者必須明確聲明來源機已 detach。真正的跨機單一擁有者留給未來 network AOS。

## AOS 自己的生命週期候選

```text
stopped -> recovering -> ready -> draining -> stopped
```

- 啟動時先取得 store 排他權並建立新啟動世代。
- recovery 完成前不派新工作。
- draining 後不收新工作，只讓現有工作到安全邊界。
- crash 後，舊世代 worker 晚回來也不能提交結果。
- 有 dispatch intent 卻無完整結果證據時標 unknown，不嘗試接回舊 PID。

## 建議演進順序

1. Python 先用普通檔案與 journal 看清狀態轉移及 crash。
2. 用同一組測試比較 SQLite。
3. 正式中央 Runtime 暫選方案 B：SQLite 索引資料加普通大型輸出檔。
4. agent repo 採 `.aos/` 資料夾，只保存不可修改的 checkpoint。
5. 先做好 Git 離線搬移；方案 C 與 network AOS 之後再做。

## 尚待原型

- exporter 能否保存 messages／Receipt，同時拒絕 PID／FD／claim。
- process 還在跑時，`pause` 是否只回「請求已接受」，而不誤稱 paused-safe。
- 在 staging、rename、中央 commit、更新 `.aos/HEAD` 各點殺掉 writer，是否只看見完整舊版或完整新版。
- checkpoint → detach → Git clone 到新路徑後，是否保持 detached／paused 且啟動工作次數為 0。
- Definition 改變後，attach／resume 是否明確擋下版本不符。
- 第二個 manager 是否會被 store 排他權拒絕；舊世代 Receipt 是否被拒絕。
- 普通檔案與 SQLite 跑相同 crash fixtures 後，恢復結果是否一致。
