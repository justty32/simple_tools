# AOS 排程與派發

狀態：設計提案，尚未實作。Task／Step 基本定義見 [`AOS-ARCHITECTURE.md`](AOS-ARCHITECTURE.md)。

## Execution queue

Execution queue 是同類 Steps 等待執行的位置。一個 Step 進入 queue 時已經有不可變的輸入快照，queue 只
保存 Step reference 與順序，不複製完整資料。

Step 第一版明確指定 target queue，例如：

```text
linux-local       一般本機程式
endpoint-x        指定的 LLM endpoint
compiler-pool     可執行編譯工作的 workers
```

一個 LLM ask Step 指定 `endpoint-x` 後，AOS 先保存 Step，再放進 `endpoint-x` queue，等待能處理該 queue
的 executor。未來若 Step 只描述所需能力，可由 routing policy 選 queue；v0 先不做隱含選擇。

## Executor

Executor 是實際消耗 queue 的東西，例如內建 Linux runner、某個 endpoint adapter，或日後的 GPU worker。
它向 AOS 註冊：

```text
executor 編號
可處理的 queues
目前容量
執行狀態
```

manager 派發 Step 時產生 claim。executor 完成後提交 stdout、stderr、exit status 或 signal 等 receipt。
executor 不能直接改 Task／Step 中央狀態，也不能自行從磁碟取走尚未派發的 Step。

多個 executors 可以處理同一 queue；單一 executor 也可以處理多個 queues。前者需要選 worker，後者需要在
競爭 queues 之間分配容量，兩者都由 AOS policy 決定。

## Function

Function 是可重複使用的 Step 目標定義，例如 executable 及其預設 target queue、cwd、環境與限制。它是
建立 Step 的便利註冊表，不是 queue 裡的執行實例。

Step 進入 queue 前，Function 必須解析成明確的 target queue、executable、argv 與 stdin。一般 Linux 程式
可直接成為 Function；HTTP 或 IPC 則可透過 executable adapter 保持相同的 Step 介面。

## Task 與 Step 必須一起考慮

AOS 不是先孤立地選 Task，再忽略 Step 條件硬跑下一步。scheduler 建立的每個候選至少包含：

```text
Task 編號與 Task priority
Step 編號與 Step priority
target queue 與排隊時間
可以接手且仍有容量的 executor
```

policy 從中選出 `(Task, Step, queue, executor)`，或暫時不派發。只有以下條件同時成立才是候選：

1. Task 不是 paused、stopped 或終止狀態；
2. Step 是該 Task 目前可執行的下一步；
3. Step 已完整保存並位於 target queue；
4. 至少一個可處理該 queue 的 executor 有容量；
5. 同一 Task 沒有另一個 running Step。

某個高優先級 Task 若只剩 endpoint Step，但 endpoint 暫時沒有容量，不應阻塞其他 queues 上可以執行的
Steps。換句話說，Task priority 不能讓不可執行的 Step 造成全域 head-of-line blocking。

## 兩層 priority

Task priority 表示整項工作的相對重要性，會影響它的所有 Steps。Step priority 表示目前這一條指令本身的
急迫性。兩者是不同欄位，都必須出現在 policy 輸入中。

核心不把兩者寫死成一個加總公式。v0 先使用容易驗證的固定順序：

```text
Task priority 由高到低
-> Step priority 由高到低
-> Step 進入 queue 的時間由早到晚
```

priority 同值才比較下一欄。之後可替換成 weighted policy、Task fairness、aging、deadline、rate limit 或
資源預算，不改 Task／Step 格式。若長期高 priority Tasks 造成飢餓，aging 或配額應由新 policy 解決。

## 一次派發

```text
建立所有可派發候選
-> policy 選出候選與 executor
-> manager 重新驗證 Task、Step、queue 與 capacity
-> 建立 claim，Task／Step 改成 running
-> executor 執行
-> manager 保存 receipt
-> 套用 pause／stop request
-> 有下一步：放進下一步的 target queue
-> open 且沒有下一步：paused
-> closed 且沒有下一步：completed
```

policy 只取得不可修改的候選快照。它不能直接改檔、拿 manager lock、建立 claim 或執行 Step。實際狀態轉移
永遠由 manager 完成。

## 第一版的並行範圍

每個 queue 有 `max_running`，executor 也有自己的 capacity。manager 可以同時派發多少 Steps，取兩者及其他
限制的最小值。同一 Task 在 v0 仍最多一個 running Step，即使不同 queues 都有空位也不平行執行同一 Task。

日後若 Task 明確描述 Step dependency graph，才考慮同一 Task 多個 ready Steps。那時 Step priority 也能在
同一 Task 內決定先後；目前一連串 Steps 的模型只有最前面未完成的一個具備候選資格。
