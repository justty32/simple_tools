# C++／Janet 固化候選

> 本頁整理 `cpp_janet_phase_plan` 支線。這是 Python 原型之後的驗證路線，不是已定語言 ABI。

## 核心分工候選

一句話版本：

> C++ 掌管會改變正式狀態的硬邊界；Janet 讀取已驗證資料，只提出決策建議。

### C++ 責任

- `fork`／`exec`、argv、stdin／stdout／stderr、`wait` 與 termination。
- fd、signal、`CLOEXEC` 與 process tree 的 Linux 細節。
- file lock、inode、`O_NOFOLLOW`、bounded read 與拒絕 symlink／FIFO。
- file fsync、atomic rename、directory fsync、追加紀錄與 torn tail 修復。
- 嚴格證據驗證器、hash 與固定 JSON bytes。
- 最終 Task event 與 Receipt 的 durable commit。

### Janet 責任

- 讀取 C++ 已驗證、不可修改的 snapshot。
- 提出純規則建議（policy proposal），例如下一段順序、成功條件、等待或停止。
- 方便快速替換規則、跑 REPL 與比較多套 agent policy。

Janet 不取得 store path、fd 或寫入 handle，也不能直接提交正式狀態。C++ 收到建議後必須重新檢查版本與規則，再決定是否落盤。

Janet 的 `marshal` 可作本機暫存或除錯，不當 portable Task state；正式可攜資料仍需明確、可跨版本驗證的格式。

## 中央保存方式三案

### 普通檔案是權威

優點：人能直接閱讀；最適合 crash matrix、Python／C++ 差分與 Git 觀察。

缺點：跨多檔一致更新與大量查詢較麻煩。

建議：C++ 第一版先做這案，和 Python 逐 byte 比對。

### SQLite 是權威

優點：transaction、索引、queue 與多筆更新成熟。

缺點：較難人工 diff；DB 與大型輸出檔的發布順序仍要測。

建議：普通檔案語意穩定後，再用同一測試比較。

### 普通檔案權威，加衍生 SQLite index

普通檔案保存真相，SQLite 只加速 `task list` 等查詢，遺失時可重建。

優點：保留可讀權威，也得到快速查詢。

缺點：要處理 index 落後，且不能讓讀者誤把 index 當第二份真相。

## C++ 與 Janet 的四種接法

### A. C++ 程式內嵌 Janet

C++ 建立 Janet VM，直接呼叫 policy function。

優點：部署可集中在一個 Runtime；資料轉換與呼叫成本低；適合最後整合。

缺點：Janet crash 與 C++ 在同一故障範圍；多 thread／fork 前後的 VM 狀態要很小心；建置與升級較緊密。

定位：最終候選之一，不作第一種接法。

### B. Janet 主程式載入 C++ native module

優點：Janet REPL 與 policy 開發最舒服；C++ kernel 可包成模組。

缺點：容易讓正式寫入權不小心移到 Janet；Windows DLL 載入後的替換與檔案鎖也麻煩。

定位：適合開發工具，不優先當唯一 Runtime。

### C. Janet FFI 呼叫小型 C ABI

優點：能快速試介面；C ABI 面積可以保持很小。

缺點：pointer 壽命、ABI 版本與 callback 容易出錯；錯誤可直接變成 segmentation fault；邊界資料仍要驗證。

定位：短期小原型，不因跑通就定為正式方案。

### D. 兩個 process 用固定 JSON 溝通

C++ 把已驗證快照寫給 Janet process；Janet stdout 只回規則建議。

優點：故障隔離最好；兩邊可獨立測試與替換；很容易記錄輸入輸出作對照測試。

缺點：每次啟動與 JSON 編解碼有成本；不適合高頻細粒度 callback；需要固定 timeout、stderr 與非零 exit 行為。

定位：第一個整合原型推薦。先把語意證明清楚，再比較是否值得內嵌。

## 當前工具鏈事實

- Windows Janet 1.41.2 的既有 policy 測試為 25 項通過。
- WSL 的 C++ P0 可執行。
- 目前 WSL 沒有可用的 Linux Janet executable、header 與 library。

因此現在可以準備介面與測試資料，但不能聲稱已完成 Linux C++／Janet 內嵌固化。先檢查工具鏈；找不到時回報缺件，不用 Python 模擬冒充 Janet 證據。

## 由小到大的執行階段

1. **Python 語意完整**：先完成 process 呼叫邊界、嚴格證據驗證器、Receipt 與 crash 測試表。
2. **Linux Janet toolchain gate**：在 WSL 確認版本、header、library、最小 compile／run。
3. **C++ process kernel**：固化 argv／streams／termination、fd／signal 與 process lifecycle。
4. **C++ 證據層**：重做落盤順序、嚴格證據驗證器、hash 與故障點測試。
5. **Janet 純規則**：只讀已驗證快照，輸出建議；錯誤不改 store。
6. **四種接法等量小原型**：用同一測試資料比較 process、內嵌、native module、FFI。
7. **C++ 以檔案為唯一真相**：先跑完 Python／C++ 差分，再比較 SQLite。
8. **兩段呼叫固化**：同一輸入在 Python、C++／Janet 得到逐 byte 相同的 Call、Receipt 與最終投影。

每一階段通過後才往上組，不必等完整 agent 才驗最底層 Linux 行為。

## 各種接法的共同契約候選

Janet 的輸入只能是已驗證的值，不是即時 store view。輸出是建議，例如：

```text
policy_version
observed_state_ref
decision
ordered_calls
reason
```

C++ 應拒絕：未知欄位、重複 key、過大輸出、錯誤版本、過期 state ref、非法 path／argv，以及 policy 自行捏造的 Receipt。

Janet timeout、crash、stderr 或格式錯誤 JSON，都只能讓 Task 停在可診斷狀態；不能留下半份正式記錄。

## 尚待原型

- Python 與 C++ 對相同 argv、raw bytes、exit／signal／launch error 是否完全一致。
- C++ 證據驗證器是否對相同證據樣本集給出和 Python 相同的「結果已知／結果不明／資料矛盾」分類。
- 每個 fsync／rename／event commit 點殺掉 writer，重啟是否只有完整舊狀態或完整新狀態。
- Janet 嘗試回傳非法 child Call、過期版本或偽造 Receipt 時，C++ 是否拒絕且零寫入。
- 四種接法對同一快照是否回相同建議／錯誤；process 接法被殺時 store 是否不變。
- embed 模式在多 thread 與 fork 邊界是否能安全初始化、重建或明確禁止。
- Windows DLL 被載入後能否順利重建；若不能，是否只作開發選項。
- 固定 JSON、hash 與兩段呼叫的最終投影能否跨 Python／C++／Janet 逐 byte 相同。
- 以普通檔案與 SQLite 跑同一組一萬 Task 查詢及 crash tests，量出恢復與除錯成本。
