# AOS 核心架構

狀態：設計提案，尚未實作。第一版範圍見 [`AOS-V0.md`](AOS-V0.md)。

## 一句話定義

AOS 是 Linux 上的 Task 排程與 Step 派發系統：一個 Task 是一連串 Steps；一個 Step 是一次完整的 Linux
行程呼叫。

```text
Task = Step 1 -> Step 2 -> ... -> Step N

Step = executable + argv + stdin
    -> stdout + stderr + exit status
```

AOS 本身與 agent loop 無關。它不認識 LLM、message、tool call 或 Round。這些可以在 AOS 完成後，作為
使用 AOS Task／Step 介面的上層程式實作。兩者的對應另見 [`AGENTLOOP-ON-AOS.md`](AGENTLOOP-ON-AOS.md)。

## AOS 在 Linux 上做什麼

Linux 已經提供檔案系統、行程、pipe、signal、權限與裝置 I/O。AOS 不重做這些功能；它是一個常駐的
使用者層管理程式，負責：

1. 接受並保存 Task 及其 Steps；
2. 把每個可執行 Step 放進指定的 execution queue；
3. 綜合 Task、Step 與 executor 狀態作排程決策；
4. 把 Step 派給能執行該 queue 的 executor；
5. 接受並保存 Step 結果，再更新 Task 狀態；
6. 讓 Task 的下一個 Step 進入適當 queue。

```text
Task A -> Step A1 -> endpoint-x queue --+
Task B -> Step B1 -> linux-local queue --+--> scheduler --> executor
Task C -> Step C1 -> endpoint-y queue --+                   |
                     ^                                      |
                     +---------- 下一個 Step <--------------+
```

這不是兩個互不相干的 scheduler。AOS 對一個候選項的完整判斷是：哪個 Task 的哪個 Step，現在可以交給
哪個 executor。Task 是被管理的工作，Step 是實際進入 execution queue 並取得執行權的單位。

## Task

Task 代表一項有開始、有狀態、可以暫停或完成的工作。至少包含：

```text
Task 編號       AOS 內的穩定識別
工作目錄        Step 預設執行與讀寫的位置
狀態目錄        Task 自己的程式與持久狀態；可與工作目錄相同或分開
Steps           已提交的有序指令
目前位置        下一個尚未執行的 Step
排程狀態        created / queued / running / paused / completed / stopped / failed
Task priority    整項工作的基礎優先級
排程資料        fairness、deadline 等排程規則需要的少量欄位
```

Steps 可以在 Task 開始前全部提交，也可以執行到一半，根據前一步結果再追加。AOS 不解讀 Step 的 stdout
來猜下一步；Task 的建立者或上層狀態機負責追加 Step、要求暫停或宣告完成。

同一個 Task 同時最多執行一個 Step；在沒有明確平行語意前，只有最前面尚未完成的 Step 可以進 queue。

## Step

Step 是一筆不可分割的 Linux 行程呼叫：

```text
輸入：target queue、Step priority、executable、argv、stdin、cwd、必要的執行設定
輸出：stdout、stderr、exit status 或 signal、AOS 執行錯誤
```

argv 是字串陣列，不能先拼成 shell command。stdin、stdout、stderr 都是原始 bytes；AOS 不要求 UTF-8、
JSON，也不合併 stdout 與 stderr。Step 的輸入在進入 queue 前固定，避免排隊期間修改檔案使已提交指令
悄悄改變。

exit status 只說明這次 Linux 行程如何結束，不代表 Task 已完成。非零 exit status 也不應由 AOS 自動解讀
成整個 Task 失敗；是否繼續、改走其他 Step 或結束，由 Task 自己的規則決定。

target queue 指出應由哪一類 executor 執行，例如 `linux-local` 或某個特定 endpoint queue。Step priority
表示這一條指令本身的急迫性；它不會覆蓋 Task priority，兩者都會交給 scheduling policy。

Step 一旦開始，第一版會等待該 Linux 行程結束，不做中途搶占。pause 或 stop 在 Step 執行中提出時，只先
記錄要求，等 Step 收據完整保存後才生效。

## Execution queue 與混合排程

Execution queue 是 Steps 等待執行的位置；executor 是實際消耗 queue 的本機 runner、endpoint adapter 或
其他 worker。AOS 不把選 Task 與選 Step 拆開，而是從可派發候選中一次選出：

```text
(Task, Step, target queue, executor)
```

候選同時帶有 Task priority、Step priority、等待時間及 executor capacity。完整的 queue、executor、Function
與 policy 設計見 [`AOS-SCHEDULING.md`](AOS-SCHEDULING.md)。

## Task 狀態

```text
created --start--> queued --派給 executor--> running
                    ^                 |
                    |                 | Step 完成，還有下一步
                    +-----------------+

running / queued --pause--> paused --resume，有下一步--> queued
running / queued / paused ------------------------------> stopped
沒有下一步且已結束輸入 -------------------------------> completed
AOS 無法安全繼續 --------------------------------------> failed
```

若 Task 尚允許追加 Steps，但目前沒有下一步，它停在 paused。呼叫端可以追加 Step 後 resume。若 Task 已明確
關閉，不再接受新 Step，最後一個 Step 完成後進入 completed。`completed`、`stopped`、`failed` 都是終止
狀態，不能 resume。

## 檔案與中央狀態

Task 的工作資料與程式可以直接放在檔案系統，執行中修改它們會影響之後才建立的 Step。AOS 另外保存最小
中央狀態：Task 編號、兩個目錄的位置、execution queues、executor 狀態、Step 輸入快照、收據與 crash
recovery journal。

中央資料是排程權的唯一來源，但不應複製 Task 的完整業務狀態。工作目錄、狀態目錄，以及侵入式／非侵入式
放法見 [`AOS-INTEGRATION.md`](AOS-INTEGRATION.md)。

## 與 Linux 的界線

AOS 透過 Unix socket 接收命令，以 atomic replace／append-only journal 保存管理狀態。內建 Linux executor
用不經 shell 的 exec 執行 Step；其他 executors 透過受控的 claim／receipt 介面取得 Step 並交回結果。第一版
仍使用目前 Linux UID 的權限，沒有 sandbox；工作目錄不是安全邊界。

未來若加入 systemd user service、cgroup、namespace 或 seccomp，它們屬於 Linux 執行機制；決定何時使用、
給誰多少資源，才屬於 AOS 排程規則。

## 不可破壞的規則

1. execution queue 裡放 Step reference；完整 Step 與所屬 Task 各有唯一記錄。
2. scheduler 的候選是 `(Task, Step, queue, executor)`，必須同時符合四者條件。
3. 同一 Task 預設最多一個 queued 或 running Step。
4. Step 輸入必須先保存，才可進入 execution queue。
5. stdout、stderr 與結束狀態必須先落盤，Task 才能離開 running。
6. Step 狀態只由 AOS 推進，executor 只能提交收據。
7. exit status 不等於 Task 狀態。
8. 已送出但結果不明的 Step 不自動重送。
9. 排程規則不能繞過 manager 派發 Step。

為了 REPL、除錯或 bootstrap，仍可繞過 AOS 直接執行程式；那條路徑沒有 AOS 的排程與恢復保證。
