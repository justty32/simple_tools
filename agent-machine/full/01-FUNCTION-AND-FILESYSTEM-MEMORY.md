# Function 與 filesystem memory

本頁只建立 AOS 最小世界觀：什麼是 Function，以及 filesystem 為什麼是 memory。狀態詞義以[全套共同約束](00-STATUS-AND-SOURCES.md)為準；process 的細節留給[下一頁](02-LEAF-PROCESS-CALL.md)。

## 核心比喻

### 使用者已定

- AOS 位在 Linux 與上層工作之間。**Function 像 CPU 指令，filesystem 像記憶體**；硬碟則是 AOS 掛掉後仍在的慢速記憶體。
- Function 是可被呼叫的工作定義，可由明確的檔案或資料夾 path 承載，也能呼叫其他 Function。
- Linux process 是最通用的 leaf（組合最底層）形式，但不是 Function 的完整定義。
- 正在執行中的 Function 實例叫 Task；Function、Call 與 Task 的細分見[03](03-CALL-TASK-RETURN.md)。

### 目前推薦的解讀

這個比喻不表示「每個檔案都是 Function」，也不表示「寫入檔案一定安全」。AOS 仍須知道要呼叫哪個 Definition、讀到哪一版資料、哪些寫入已完整發布。

## Function 由小到大

### 目前推薦

| 形狀 | AOS 看見的意思 |
|---|---|
| 一個 executable file | 最小 leaf Function；一次呼叫可落成一個 process。 |
| 一個 directory module | 仍可只是一個 Function；內含 helper 不會自動產生 child Task。 |
| composite Function | 明確呼叫其他 Functions；需要 AOS 分別管理時才形成 child Tasks。 |

- 單檔變資料夾只是整理結構；只有工作真的需要獨立結果、恢復或外部作用邊界，才拆成 AOS 可見的 child Function Call。
- AOS 應從呼叫者給的明確 target 解析 Definition，不掃目錄猜 `main.py`、`run` 或 child relation。
- 一次已接受的呼叫應綁定可檢查的 Definition 版本；dispatch 時若版本不合，應停止並要求處理，不能悄悄改跑新版。

### 尚未決定

- Directory Function 採固定入口、manifest（入口設定檔）或薄 launcher；預設 `cwd` 也未定。
- Definition 包含哪些 helper、prompt、設定與依賴，以及 generation（版本世代）如何計算。
- File 變 directory 時如何保持同一呼叫外觀；任何候選 CLI 都還不是契約。

## Filesystem 裡不是只有一種 memory

### 使用者已定

- 可攜語意狀態與某台機器的 active set、queue、claim 必須分開；搬到另一台機器後，不得假裝原 process 仍連著。
- Agent root 應可進 Git，但這不代表所有 runtime 檔都該進 Git；Agent 專屬邊界由後頁處理。

### 目前推薦

| 資料 | 應回答的問題 | 基本處理 |
|---|---|---|
| Definition | 這次要跑哪版工作？ | 明列範圍並綁定版本。 |
| 可變 memory | messages、設定、成果目前是哪版？ | 有寫入權與發布邊界，避免舊工作覆蓋新版。 |
| 持久證據 | 接受了什麼、已知結果依據是什麼？ | 提交後不可原地改寫。 |
| machine-local 狀態 | 哪個 worker、PID 或 claim 仍有效？ | 只在本機重建，不冒充可攜 image。 |
| 外部世界 | HTTP、寄信或 root 外寫入是否已發生？ | 另存 effect intent 與結果證據；filesystem snapshot 不能回滾它。 |

不要用一個模糊的 `memory_mode` 包辦全部資料。概念上也應分開 Definition、memory 與 Runtime 啟動世代。

### 尚未決定

- `.aos` 的精確內容與位置、中央 store 如何引用 repo 內資料，以及哪些資料可進 Git。
- Memory mode 的正式名稱、Definition／memory generation 算法與 dependency snapshot 範圍。

## 讀寫模式的邊界

### 目前推薦

- **Live**：讀目前路徑，最符合 shell 習慣，但同一工作前後可能看見不同 bytes。
- **Checked-live**：記住 base version，在 dispatch 或發布前再比對；能偵測過期，卻不能消除「檢查後、使用前」的空窗。
- **Snapshot／staging**：讀固定副本或先寫私有區，再一次發布；較容易重現，但增加衝突、清理與磁碟成本。
- 第一版採混合思路：普通工作區可保持 live；Definition 在作用前檢查；可變 memory 以版本與 staging 發布；持久證據不可修改；外部作用另走 intent／結果證據。
- Task 產生新 Definition 時，新版只供後續明確呼叫使用，不能反過來宣稱本次就是用新版執行。

### 原型暫選

- Process fixture 暫以 absolute executable path 加 SHA-256 表示 leaf generation。它只證明某次檢查所見的單一檔案內容，不證明實際 `exec` bytes，也不涵蓋 script、shared library、helper 或 directory Definition。
- P1 的 `<store>`、JSON、hash、固定 ID 與檔名只是 crash workbench，不是 `.aos` 或 filesystem memory ABI。

### 明確延後

- 完整 workspace overlay、自動合併、多 writer 共寫 live root、NFS、跨機同步、symlink、外力改名與 path reuse。

候選的完整取捨可追到 [filesystem memory 工作稿](../workbench/2026-08-14/idea-ledger/FILESYSTEM-MEMORY-CANDIDATES.md)與 [Function 成長工作稿](../workbench/2026-08-14/idea-ledger/MODULE-EVOLUTION-CANDIDATES.md)；兩者都不是正式規格。
