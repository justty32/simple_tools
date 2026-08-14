# AOS v0：Task／Step 排程器

狀態：設計提案，尚未實作。核心定義見 [`AOS-ARCHITECTURE.md`](AOS-ARCHITECTURE.md)。

## 第一版的目的

先把獨立於 agent loop 的 AOS 做出來。AOS v0 是每位 Linux 使用者一個常駐 manager。它保存 Tasks，把
下一個 Step 放入指定 execution queue，再交給可以處理該 queue 的 executor。

第一版要有：

- 一個 manager 與 Unix socket；
- Task 的建立、追加 Step、start、pause、resume、close、stop 與查詢；
- 以 argv、stdin、stdout、stderr、exit status 為核心的 Step 格式；
- 多個具名 execution queues；
- 一個內建 Linux executor，可被設定成消耗指定 queues；
- 同時看 Task priority、Step priority 與 executor capacity 的 scheduler；
- Step 輸入與結果的持久化；
- manager 重啟後的保守恢復；
- 工作目錄與狀態目錄可分開指定。

agent loop、LLM endpoint、tool call、Bot 目錄、workflow、GPU、rate limit、sandbox 與 dependency 不放進 v0。
但 v0 的 queue／executor 介面必須足以讓下一階段加入 endpoint executor，不必改 Task／Step 格式。

## v0 的 Task

Task 可先提交完整 Steps，也可執行到一半再追加。Task 有自己的 priority；每個 Step 另外有 Step priority。

除了排程狀態，Task 還有輸入狀態：

```text
open     之後仍可追加 Step
closed   不再接受 Step；最後一個 Step 完成後即可 completed
```

排程狀態使用：

```text
created  queued  running  paused  completed  stopped  failed
```

規則如下：

1. `start` 時有待執行 Step，就把第一個 Step 放入指定 queue，Task 進入 queued。
2. scheduler 綜合 Task、Step 與 executor，選出一個可派發候選。
3. executor 完成 Step 後，AOS 保存收據。
4. Task 仍有下一步，就把該 Step 放進它指定的 queue。
5. Task 是 open、但暫時沒有下一步，就進入 paused，等待追加 Step。
6. Task 是 closed、而且沒有下一步，就進入 completed。
7. paused Task 追加 Step 後，必須明確 resume 才重新入 queue。

close 與 pause 是兩件事。close 表示「不會再有新 Step」；pause 表示「現在不要執行」。close 一個仍有 Steps
的 Task 不會丟掉它們，而是在執行完後完成。

## Step 格式

每個 Step 都是：

```text
target queue + Step priority
executable + argv + stdin -> stdout + stderr + exit status
```

v0 的 Step 必須明確指定 queue。AOS 不根據 executable 猜 queue。進入 queue 前先保存 target queue、
executable、argv、stdin 與 cwd；argv 分開傳遞，三個標準串流按原始 bytes 保存。

只要 executor 已可靠回報 exit status 或 signal，Step 就算完成。非零 exit status 不會讓 AOS 自行判定整個
Task failed，也不會自動跳過後續 Steps。上層讀取收據後自行決定。

若 manager 或 executor crash，使 running Step 的結果無法確認，該 Step 標成結果不明，Task 進入 failed，
不自動重送，避免重複副作用。

## Execution queue 與 Executor

v0 先提供具名 queue：

```text
queue name       穩定名稱，例如 linux-local、endpoint-a
max running      這個 queue 可同時派發的 Step 數
executor set     可以消耗此 queue 的 executors
```

內建 Linux executor 使用不經 shell 的 exec 執行 Step。v0 可把它同時註冊到兩個測試 queues，以驗證 queue
routing；真正的 endpoint executor 到 agent loop 階段再加入。

executor 必須先取得 manager 發出的 Step claim，完成後提交 stdout、stderr 與 termination receipt。只有
manager 可以推進 Task／Step 狀態；executor 不能直接改中央檔案或自行取走未派發的 Step。

## 混合排程規則

scheduler 不是先挑一個 Task，再無條件執行它的 Step。每個候選都是：

```text
(Task, Task priority, Step, Step priority, target queue, available executor)
```

只有 Task 未暫停、Step 是該 Task 的下一步、queue 有容量，而且至少一個 executor 可用時，候選才可派發。
v0 在所有可派發候選中使用：

```text
Task priority 由高到低
-> Step priority 由高到低
-> Step 進入 queue 的時間由早到晚
```

這只是第一條 policy，不是 AOS ABI。policy 接收不可修改的候選快照，回傳候選及 executor，manager 重新驗證
後才派發。某個高優先級 Task 的 target queue 沒有容量時，不可阻塞其他 queues 的可派發 Steps。

## 中央目錄

```text
$XDG_RUNTIME_DIR/aos/
  manager.sock
  manager.lock

$XDG_STATE_HOME/aos/
  queues.json                  queue 設定與 executor 對應
  scheduler.jsonl             enqueue、claim、receipt 與狀態轉移
  manager.log
  tasks/
    t-000001/
      task.json                目錄、priority、open／closed
      state.json               狀態、目前 Step、pause／stop request
      steps/
        000001/
          step.json            queue、priority、executable、argv、cwd、狀態
          stdin
          stdout
          stderr
```

execution queues 只保存 Step reference 與順序，不複製完整 Step。`task.json`、`state.json` 與 queue journal
只由 manager 寫。工作目錄是 Step 的預設 cwd；狀態目錄的內容由呼叫端管理，AOS 不解讀。

## 狀態轉移

```text
Task start
    |
    +-- 下一個 Step -> 指定 queue -> queued
    +-- open，沒有 Step ----------------------> paused
    +-- closed，沒有 Step --------------------> completed

(Task, Step, queue, executor) 被選中 -> running
                                         |
                                         +-- pause request -> paused
                                         +-- stop request --> stopped
                                         +-- 有下一步 -----> 下一個 queue
                                         +-- open，無下一步 -> paused
                                         +-- closed，無下一步 -> completed
                                         +-- 結果不明 -----> failed
```

v0 不切斷 running Step。pause、stop 遇到 queued Step 時可以立即從 queue 移除；遇到 running 時只記錄
request，在 Step 收據完整保存後生效。

## 初始命令草案

```sh
aos manager start
aos manager status
aos manager stop

aos queue add QUEUE --executor linux --max-running 1
aos queue list

aos task create --priority N --workdir WORKDIR [--state-dir STATE_DIR]
aos task append TASK --queue QUEUE --priority N --stdin INPUT -- EXECUTABLE ARG...
aos task start TASK
aos task status TASK
aos task pause TASK
aos task resume TASK
aos task close TASK
aos task stop TASK
aos task list
```

`append` 中 `--` 後第一個字串是 executable，其餘保持為 argv，不經 shell 重新解析。省略 `--stdin` 時
stdin 為空 bytes。API 還應提供原子的 append-and-resume，避免「追加下一步並繼續」的中間競態。

## 實作順序

1. Task、Step、queue 的落盤格式及純狀態轉移測試。
2. 候選建立與混合 priority policy 的純函式測試。
3. 內建 Linux executor、claim 與 receipt。
4. manager、Unix socket 與 CLI。
5. pause、resume、close、stop 與 crash recovery。
6. 多 Task、多 queue 驗收後，再開始 [`AGENTLOOP-ON-AOS.md`](AGENTLOOP-ON-AOS.md)。

## 完成條件

1. 一個 Task 能依序執行兩個 Steps，並保存每個 Step 的完整收據。
2. 不同 target queues 的 Steps 只會交給已註冊且有容量的 executor。
3. scheduler 同時收到 Task priority 與 Step priority，並按 v0 policy 選擇。
4. 高優先級 Task 的 queue 忙碌時，不會阻塞其他 queue 可執行的 Step。
5. open Task 用完目前 Steps 後 paused；append-and-resume 後可繼續。
6. closed Task 用完 Steps 後 completed，不能 append 或 resume。
7. 非零 exit status 被保存，但不會被誤當成 Task 狀態。
8. pause／stop 不切斷 running Step，只在 Step 邊界生效。
9. crash 後恢復 queued／paused；結果不明的 running Step 不重送。
10. queue 只保存 Step reference，完整資料只有一份權威記錄。
