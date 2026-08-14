# Bot 目錄與行程構想

這裡記錄尚未定案、尚未實作的想法。內容不是目前程式的行為契約。

## 一個 Bot 就是一個持久行程

在 AOS 中，一個 Bot 本質上是一個行程。它和一般 Linux process 的差異是：它的工作記憶體與
持久化儲存不分居在 RAM object 和另一套 database，而是由同一個檔案系統目錄表示。

```text
bot-a/ = 行程本身 = 可修改的記憶體映像 + 持久狀態 + 可執行入口
```

因此建立 Bot 並不只是產生一份設定檔：

```sh
aos new bot-a
```

這是在 AOS 的檔案系統記憶體中配置一個新的、尚未執行的邏輯行程。`bot-a/` 內先放置它日後取得
CPU 時需要的程式與設定，例如 LLM、messages、tools、policy、workspace，以及 `start`、`status` 等
公開入口。此時它可以存在、被閱讀、修改、複製或版本化，但尚未進入中央排程。

## start 讓行程成為可排程工作

```sh
bot-a/start "some task"
```

這個操作同時發生兩類變化：

1. 在 `bot-a/` 裡加入或更新此次執行需要的檔案，例如 task、目前狀態、messages、journal、結果與
   scratch。這些檔案就是行程正在運作的記憶體，也同時是可在重啟後恢復的持久狀態。
2. 向中央 AOS manager 註冊 `bot-a`，讓 manager 將這個行程納入 endpoint／CPU 排程。

概念上的生命週期是：

```text
aos new bot-a
        |
        v
bot-a/ 已存在，但尚未排程
        |
        | bot-a/start "some task"
        v
寫入 task 與執行期檔案 -> 向中央 manager 註冊 -> schedulable
        |
        v
manager 派發一條 instruction 給 endpoint／CPU
        |
        v
結果回寫 bot-a/ -> 再次等待排程，或進入 waiting／done／failed
```

`bot-a/` 是有持久身分與狀態的邏輯行程；`"some task"` 是這個行程目前收到的工作，不需要再把 task
升格成另一個與 Bot 並列、要求使用者管理 ID 的公開物件。一個 Bot 同時只處理一份目前工作，後續工作仍可
沿用同一目錄裡的記憶。

## 權威狀態與中央 registry

`.aos/` 位於工作目錄內時，以該目錄保存工作流狀態；放在外面時，則以行程表指定的 AOS 目錄為準。
中央 manager 另保存排程真正需要的 registry／queue 資訊，例如：

- 哪些 Bot 已註冊且可被排程；
- Bot 目錄的位置或穩定識別；
- priority、budget、等待原因與下一條可執行 instruction；
- 哪個 endpoint 目前有 capacity，以及 instruction 是否正在執行。

中央 registry 不應悄悄複製另一份完整 Bot state，否則「記憶體與持久化在同一個地方」會重新裂成兩個真源。
故障恢復必須能以 AOS 目錄、中央行程記錄與落盤日誌重建，不能依賴仍活著的 Python object、thread 或
Controller。

Controller 也不擁有行程。它只是對中央 manager 提出 `pause`、`continue`、`send`、`stop` 等要求，並從
Bot 目錄或 manager 的 view 觀察狀態。真正決定何時取得 endpoint／CPU 的權力留在中央 scheduler。

## 待確認的邊界

- `aos new` 是否立即在中央建立 dormant registry entry，或等第一次 `start` 才註冊。現在的直覺是
  第一次 `start` 才納入排程；純粹存在於檔案系統中的 Bot 不需要中央常駐追蹤。
- registry 以 canonical path 指向 Bot 是否足夠；Bot 在 active 時被 `mv`、複製或刪除要如何處理。
- 哪些執行期檔案必須在 `start` 時才產生，哪些應由 `new` 明確建立空檔，讓目錄結構可預期。
- task 完成後 Bot 是保留為 dormant registered process，還是從中央 registry 移除、下次 `start` 再註冊。
- 中央 scheduler 的 durable queue 放在系統目錄，還是 Bot 目錄只保存 submission、manager 依提交紀錄重建。

這些問題不改變核心方向：Bot 行程的程式、記憶體與持久狀態共同位於 `bot-a/`；中央 AOS 管理執行權
與 CPU 排程，但不取代 Bot 目錄成為第二份完整狀態真源。

## 下一輪設計的名稱與介面起點

對外名稱統一改為 **AOS**，command／binary 使用小寫 `aos`。舊筆記中的 Agent OS、AgentOS 與
`agentos` 都視為歷史名稱；下一輪整理正式設計時再一致遷移，不讓這次的筆記修改假裝舊介面已經實作。

```sh
aos new bot-a
```

Bot 內原本的 `./run` 太偏向一次 runtime instance，也延續了「Run 是公開物件」的舊概念。既然目前模型是
「Bot 是持久行程，task 是它目前收到的工作」，公開控制成員也應改用 task 語彙。候選外形是：

```sh
./bot-a/task next
./bot-a/task pause
./bot-a/task continue
./bot-a/task send "more context"
./bot-a/task stop
```

目前只確定 `./run` 要改成與 task 有關的名稱；`./task` 是否為最終名稱、`status`／`answer`／`log` 要留在
獨立成員還是收進 `task`，留待下一輪從整體資料夾結構一起設計。`./bot-a/start "some task"` 暫時保留：
`start` 是把工作交給 Bot 並向中央 scheduler 註冊，`task` 則控制已提交的目前工作。

第一輪具體提案後來已再修正，現行版本見 [`AOS-V0.md`](AOS-V0.md)。

## 現行修正：AOS 排程 Task

AOS 管理 Task，而 execution queue 放 Step reference；一個 Task 是一連串 Steps，一個 Step 是一次 Linux
行程呼叫：

```text
Task 的下一個 Step -> 指定 execution queue -> 可執行它的 executor
```

排程不能拆成互不相關的「選 Task」與「選 Step」。AOS 同時考量 Task priority、Step priority、target queue
與 executor capacity，再選出 `(Task, Step, queue, executor)`。因此 endpoint ask 會先進入指定 endpoint 的
queue，等能處理該 endpoint 的 executor 執行。

AOS 本身獨立於 Bot 與 agent loop。agent loop 建在 AOS 上時，一個 Round 對應一個 Task；endpoint ask 與
每一次 tool execute 都各是一個 Step。Task 可以在自然靜止時 pause，之後接受指令、追加 Step，再 resume。
完整核心模型見 [`AOS-ARCHITECTURE.md`](AOS-ARCHITECTURE.md)，agent loop 對應見
[`AGENTLOOP-ON-AOS.md`](AGENTLOOP-ON-AOS.md)。

`~/repo/workflows` 進一步確定 agent loop 的狀態目錄形狀：工作目錄旁可以有類似 `.claude/` 的 `.aos/`，
也可以放在外面。這是 agent loop 層慣例，不是 AOS 核心 ABI；queue 與 running Step 仍只放在中央。
