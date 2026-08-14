# Leaf process call

本頁只定最底層 Linux process Function 的可觀察邊界；它不把 process 提升成所有 Function，也不決定 JSON、CLI 或 Task 生命週期。上層概念見[01](01-FUNCTION-AND-FILESYSTEM-MEMORY.md)，角色切分見[03](03-CALL-TASK-RETURN.md)。

## 最小模型

### 使用者已定

```text
argv + stdin -> stdout + stderr + termination
```

- Linux process 是通用 leaf Function，也是 Tool 很好的共同底座。
- `stdout`、`stderr` 與 termination 是不同觀察面；它們不能因為方便就合成一段文字。
- 這只是 leaf。Directory、composite、Step、Round 或 Agent Function 不能被縮成「一個 PID」。

### 目前推薦

一次 process Call 在語意上至少分開：

- 已解析的 executable、`argv` 陣列與 raw-byte `stdin`；不得先拼成 shell 字串。
- 明確的 `cwd` 與 environment policy。`cwd` 只改相對路徑起點，不是 sandbox。
- raw-byte `stdout`、raw-byte `stderr`，以及獨立的執行結論。
- 執行結論至少能分辨正常退出及 code、被 signal 結束、以及在 `chdir`／`exec` 等啟動階段失敗。

**尚未決定：**是否搜尋 `$PATH`、`argv[0]` 能否另設、environment 是繼承或白名單，都尚未成為 ABI。

**目前推薦：**Shell sugar 若存在，先降成上述明確 Call，再進 AOS 核心。

## 已知結果不等於成功

### 目前推薦

- `exited(0)`、非零 exit、signal 與可辨識的 launch error 都可能是**完整且已知**的 leaf Return；它們不是同一種 termination。
- 「證據完整」只表示 AOS 知道 leaf 發生了什麼。呼叫者仍須用自己的 policy 判斷是否達成目的。
- Process 結束不等於整個 Task 完成；composite parent 可能還要觀察其他 children、組合結果或發布 memory。
- Process 退出也不保證它 fork 出去的 background child 已停，因此不能只靠 `waitpid` 宣稱 root 已安全暫停。

## 證據與結果不明

### 目前推薦

外部作用前先安全保存 dispatch intent。完成後，可信 leaf 證據應把原 Call、分離的兩條輸出、termination 與本次執行依據綁在一起，再供 Task 建立持久結果。

以下情況不能拼成成功：

- 已有 intent，但沒有能一起驗證的完整結果。
- 只有 stdout、只有一半檔案，或輸出與摘要不符。
- manager 消失後只看見舊 PID、暫存檔或無法證明來源的結果。
- executable 在檢查後被替換，而證據只記得舊 path／hash。

此時標為 **unknown（結果不明）**，保留現有證據，不自動重跑。因為 process 可能已寄信、呼叫 API 或修改外部資料；重開不是重做許可。

### 尚未決定

- Common Function Return 如何包裝 termination 與兩條輸出，以及 error／unknown 的公開名稱。
- Timeout、取消、resource limits、process group、environment snapshot、大型或串流輸出的正式規則。
- 如何證明實際執行 bytes：再次 hash 只能縮小空窗，snapshot 或固定 fd 對 script／動態依賴也仍要另驗。

## 原型到底證了什麼

### 原型暫選

- [Python P0](../workbench/2026-08-14/p0-function-python/README.md) 以 `shell=False` 實跑 process，驗到 argv、binary stdin、兩條輸出與 termination 可分開保存；有 intent 而缺完整結果時，recovery 不重跑。
- [C++ P0](../workbench/2026-08-14/p0-function-cpp/README.md) 以 `fork/execve`、nonblocking pipes 與 `poll` 對照 Python，驗到 stdin 與兩條輸出必須同時推進，也分開正常 exit、signal、`chdir`／`exec` 失敗。
- 兩個原型對 launch failure 的 JSON 名稱都不同，正好說明測通 process 邊界不等於格式已固定；它們的 CLI、目錄、ID、marker 與 receipt 檔名均非 ABI。
- P1a-2 的 process seam 目前是待驗工作契約；已通過的 58 項 P1a-2A 使用 fake children，沒有執行真 process，不能拿來補強 P0 以外的聲稱。
- 現有證據只涵蓋所列 Linux／WSL process-kill 測試；`write`、`fsync`、rename 或測試綠燈都不能自動外推到斷電與其他檔案系統。

### 明確延後

- 安全 sandbox、secrets、完整 process-tree 收割、任意大串流、power-loss 保證、NFS、多 writer 與 exactly-once。
