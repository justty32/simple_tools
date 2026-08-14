# Agent Machine 構想筆記

這裡記錄尚未定案、尚未實作的想法。內容不是目前程式的行為契約。

目前具體操作介面候選見 [`INTERFACE.md`](INTERFACE.md)，精確指令行為見
[`COMMANDS.md`](COMMANDS.md)，設定解析見 [`CONFIG.md`](CONFIG.md)，shell 輸出見
[`OUTPUT.md`](OUTPUT.md)，三個 Bot 成員見 [`MEMBERS.md`](MEMBERS.md)，messages 見
[`MESSAGES.md`](MESSAGES.md)，runtime 見 [`RUNTIME.md`](RUNTIME.md)，crash/replay 見
[`RECOVERY.md`](RECOVERY.md)，Run 限制見 [`LIMITS.md`](LIMITS.md)，使用場景檢查見
[`SCENARIOS.md`](SCENARIOS.md)，private member composition 見 [`STORAGE.md`](STORAGE.md)。

## 以 FUSE 封裝資料與行為

最早的 prototype 以三份公開 JSON 組成 Bot；以下只是 FUSE 想法的起點，不是最新 private layout：

```text
llm.json
messages.json
tools.json
```

未來可以考慮用 Linux FUSE 將它們呈現成沒有副檔名的節點：

```text
bot-a/
  llm
  messages
  tools
```

這些節點不只是被動資料，而是把資料與相關行為封裝在一起：

- `cat llm` 或一般 `read` 會得到 JSON。
- `write` 仍以 JSON 作為交換格式。
- 寫入時先檢查 JSON 語法與該節點的資料格式；不合法就拒絕，不改變正式狀態。
- `llm`、`messages`、`tools` 各自可以有不同的驗證與求值行為。
- 讀取結果可以由底層程式動態產生，不一定直接對應某個實體 `.json` 檔案。
- 節點還可以提供 subcommands，讓使用者操作與該資料相連的行為。

概念上，這會讓一個節點同時具備：

```text
可讀資料 + 可驗證寫入 + 可呼叫行為
```

它比「另外建立資料檔，再建立一套完全分離的操作 API」更接近 Agent Machine 想要的函數式
檔案空間：資料、行為和檢查規則由同一個抽象負責。

## 尚未決定

- FUSE 是第一版核心，還是 host filesystem 之上的後續介面。
- 寫入應在 `write`、`flush`、`fsync`、close 或 rename 哪個邊界驗證與提交。一般編輯器可能分段寫入，
  也可能先寫暫存檔再 rename，因此不能假設一次 `write` 就收到完整 JSON。
- 驗證失敗時如何讓 shell、editor 和程式可靠看見錯誤，同時保留原本有效資料。
- 多個 process 同時開啟及寫入同一節點時的隔離與衝突規則。
- `$ref`／`$env` 要在讀取時展開、寫入時保存原文，還是同時提供 raw 與 resolved view。
- subcommand 的具體表面尚未決定。單一 Linux regular file 若 `cat` 得到 JSON，不能直接假設
  `./llm subcommand` 也能執行；可能需要 Agent Machine CLI、額外 command endpoint、目錄形式，或其他
  Linux 機制。先保留「節點能提供 subcommands」這個需求，不提前選實作方法。
- 真實持久資料放在哪裡，以及 FUSE unmount／daemon crash 後如何恢復。

在上述問題釐清前，保留目前普通 JSON 檔案與 CLI 實作，不開始 FUSE prototype。

## 檔案系統是記憶體，Step 是 CPU 指令

AOS（舊筆記稱 Agent OS／AgentOS）可以沿用 Lisp machine 的直覺，把檔案系統視為可直接觀察、組合與修改的記憶體，
把一次 Linux 行程呼叫視為 CPU 指令。endpoint 則是其中一種 executor：

```text
filesystem        記憶體；保存程式、資料與執行結果
Task              一項工作；由一連串 Steps 組成，具有自己的 priority
Step              一次 argv／stdin Linux 行程呼叫，具有 target queue 與 priority
AOS manager       混合考量 Task、Step 與 executor，把 Step 派到可執行它的 queue
Controller        被動控制面；觀察狀態並提出 pause、resume、append、stop 等要求
```

檔案空間應保持可塑性。人或 agent 可以直接編輯其中的 Function、prompt、tool spec、policy 與資料；
修改完成後再執行，就像 Linux 中啟動程式會把一個 task 掛到 OS，由 scheduler 決定何時取得 CPU。
這也接近 Lisp 系統能在運作中修改程式，再求值新定義的使用方式。

低階介面仍可允許呼叫者繞過 AOS，直接呼叫 Function／endpoint。這對 REPL、除錯或 bootstrap 很有用，
但不應成為一般 Task 的預設執行模式。正常路徑應更被動：Task 描述「要做什麼」，Controller
只提交控制要求，真正的 Step 執行權集中在 AOS manager。如此可由同一處處理：

- 不同 Task／Step 對 endpoint、Linux runner 與其他 executors 的競爭與公平性；
- model、token、context、timeout 與其他資源預算；
- pause、stop、priority、retry 與安全邊界；
- task 的 durable 狀態、結果提交與 crash recovery；
- 日後由 Python worker 遷移到中央 C++／Janet runtime，而不改變檔案介面。

這裡的「中央」表示排程決策有單一權威，不等於所有工作都要由一條常駐 thread 執行，也不要求第一版
立刻完成全域 scheduler。第一版仍可用按需啟動的 process 實作 manager；重要的是所有正常執行都經過同一個
提交與排程接縫，而不是讓每個 Bot 各自直接呼叫 Function、各自形成不可協調的 loop。

Bot 目錄的早期構想見 [`PROCESS-NOTES.md`](PROCESS-NOTES.md)；現行 Task／Step 模型與 Linux 邊界見
[`AOS-ARCHITECTURE.md`](AOS-ARCHITECTURE.md)。

## Bot 目錄是一個物件

預期安裝與使用方式：

```sh
sudo apt install agentos
agentos new bot-a

./bot-a/start "do something"
./bot-a/llm set temperature 0.1
./bot-a/status
```

也可以先進入物件目錄：

```sh
cd bot-a
./start "do something"
./llm set temperature 0.1
./status
```

`bot-a/` 本身就是物件。目錄中的成員遵循簡單的可見性規則：

- 公開成員主要是可執行函數，例如 `start`、`llm`、`messages`、`tools`、`status`。
- `.` 開頭的成員是私有實作與資料，例如 `.llm.json`、`.messages.json`、`.tools.json`、`.runs/`。
- 使用者透過公開函數觀察或修改私有資料，不必直接知道資料布局。
- 重型 runtime 與共用工具由 apt 安裝在系統中；Bot 目錄裡的公開函數只是薄入口。

當時記下的可能外形如下；它不是持久格式，最新介面只保證 private data 位於 `.agentos/`：

```text
bot-a/
  start
  llm
  messages
  tools
  status
  .llm.json
  .messages.json
  .tools.json
  .runs/
```

例如 `./llm` 可輸出目前 LLM 的 JSON view；`./llm set temperature 0.1` 經驗證後修改私有資料。
`./status` 則顯示這個 Bot／目前工作可供人理解的狀態。實際命令細節仍待逐步確認。

## 未來的物件導向能力

既然 Function/Bot 以目錄表示物件，未來可以再研究物件導向的組合方式：

- **封裝**：公開 executable members 操作 `.` 開頭的私有資料。
- **繼承**：一個 Bot 可以指定基底 Bot／Function，沿用其公開方法、設定預設值或工具集合，只保存差異。
- **覆寫**：子 Bot 可以在自己的目錄提供同名公開方法或設定，取代基底版本。
- **多型**：呼叫端只依賴共同方法名稱與輸入／輸出契約，例如任何 Bot 都可接受 `start`、`status`；
  實際使用哪個 LLM、工具或執行策略由各物件決定。

這些能力尚未選定實作方式。候選機制可能包括 `$ref` 組合、明確的 base metadata、symlink、mount 或
FUSE lookup 規則。開始實作前必須先確認：多重繼承是否需要、方法查找順序、private 是否真的隔離、
覆寫與升級如何被 Git 追蹤，以及基底修改後既有 Bot 是否自動受到影響。
