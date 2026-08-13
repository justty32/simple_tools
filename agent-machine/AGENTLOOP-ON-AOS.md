# 在 AOS 上實作 agent loop

狀態：設計方向。先完成 [`AOS-V0.md`](AOS-V0.md)，再開始本層。

## 相依方向

AOS 是通用 Task 排程器，agent loop 是 AOS 的一個使用者：

```text
agent loop
    |
    | 建立 Task、追加 Step、pause／resume／complete
    v
AOS
    |
    | executable + argv + stdin
    v
Linux
```

AOS 不得反向依賴 agent loop，也不應在核心中出現 LLM、message、tool call 或 Round 的判斷。

## 概念對應

```text
agent loop Round       = AOS Task
一次 endpoint ask      = 一個 Step，送到指定 endpoint queue
一次 tool execute      = 一個 Step，送到可執行該工具的 queue
Round 的持久狀態       = Task 狀態目錄中的 history、message、tool results 等資料
模型或工具的實際呼叫   = executable + argv + stdin -> stdout + stderr + exit status
```

舊 agent loop 把 `ask() -> message` 算成 Step，而把整批工具執行放在兩個 Steps 之間。建立在 AOS 上之後，
這個特例取消：每一次工具執行本身也是 Step。若模型一次要求三個工具，就是三個可個別保存與排程的 Steps。

## 一個 Round／Task

```text
Task t-42：Round

Step 1  endpoint ask -> endpoint-x queue
        -> 模型要求 read_file 與 grep

Step 2  execute read_file -> local-tool queue
        -> 保存 tool result

Step 3  execute grep -> local-tool queue
        -> 保存 tool result

Step 4  endpoint ask -> endpoint-x queue，stdin 帶入兩筆 tool results
        -> 模型產出文字，不再要求工具

        +-- 要自然完成：close Task -> completed
        +-- 要保留對話：pause Task
```

每個 Step 返回後，agent loop 狀態機先把 stdout、stderr 與 exit status 轉成自己的 message／tool result，
更新持久狀態，再決定下一個 Step。AOS 只負責排程與保存原始收據，不解析模型輸出。

Task priority 可以表示整個 Round 的重要性，例如互動工作高於背景整理；Step priority 則可調整其中一筆 ask
或工具呼叫。AOS 將兩者與 endpoint／tool queue 的容量一起判斷，不讓 agent loop 自己繞過中央 queue 直接
呼叫 endpoint。

工具之間現在也是安全邊界。AOS 可以在兩個工具 Steps 之間排入其他 Task，也能在已完成一個工具後套用
pause；已經開始的工具仍執行到 Linux 行程結束。

## pause 與繼續

Round 與 Task 是同一個物件。當模型自然停下，但呼叫端希望保留狀態以接受後續指令時，Task 不進入
completed，而是停在 paused：

```text
模型沒有 tool call
    |
    +-- auto-finish ----> Task closed -> completed
    |
    +-- keep-alive -----> Task open + paused
                              |
                              | 收到下一個使用者指令
                              v
                         更新 agent 狀態
                         追加 endpoint ask Step
                         resume 同一個 Task
```

這裡的 paused 不是終止狀態。Task 編號、Round 狀態與 history 都保留；追加指令只會建立下一個尚未執行的
Step，不會改寫已完成 Step 的輸入與收據。

若產品語意要求「每個使用者指令都是新 Round」，就應建立新的 Task，並由 agent 的長期 history 串起不同
Tasks。若選擇在自然靜止後繼續同一 Round，則 resume 原 Task。AOS 同時支援兩種做法，不替 agent loop 決定。

## agent loop 負責的部分

- 把 prompt、history、tools 與 tool results 編碼成 endpoint Step 的 queue、priority、argv／stdin；
- 解析 endpoint stdout，取得 message 與 tool calls；
- 把每個 tool call 轉成獨立 Step，指定可執行它的 queue；
- 把工具 stdout／stderr／exit status 轉回 tool result；
- 決定下一個 Step、pause、complete 或 agent-level failure；
- 維護 Round 的公開狀態、callbacks、limits 與 usage。

## AOS 負責的部分

- 保存 Task、Step 輸入與原始收據；
- 綜合 Task priority、Step priority、queues 與 executors 排程；
- 把 Step 放進指定 queue，再派給有容量的 executor；
- 在 Step 邊界實現 pause、resume 與 stop；
- manager crash 後保守恢復，不重送結果不明的 Step。

## 建置順序

1. 先完成純 Linux 程式組成的 AOS Task，不接 endpoint。
2. 實作 endpoint adapter，讓一次 ask 成為一個 Step。
3. 用單一 endpoint Step 完成最小 Round／Task。
4. 把每一次 tool execute 轉成獨立 Step，完成多步 loop。
5. 加入 Task pause、接受新指令、append-and-resume。
6. 最後才搬入 callbacks、limits、usage 與更完整的 agent 狀態機。

目前 `freepy/agentloop` 的既有文件與實作仍採「工具不算 Step」的舊定義。在本設計真正進入實作前，不讓
AOS 核心相容舊定義；等 AOS v0 穩定後，再另外規劃 agent loop 的遷移。
