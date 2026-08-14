# 使用者已明確決定的方向

> 只收錄使用者親自說過、接受或明確偏好的內容。這不是正式 ABI；實作細節若未由使用者裁決，不會在本頁補完。

## 1. AOS 的主軸

- 名稱採 **AOS**。核心抽象是「Function 像 CPU 指令，filesystem 像記憶體」。
- AOS 位在 Linux 與上層工作之間。它要讓 agent task 能用 AOS 自己的抽象思考，盡量不直接承受 PID、process 消失等 Linux 細節。
- AOS 不只管別人，也必須管理自己的生命週期；AOS 掛掉後，硬碟上的持久狀態仍要能留下來。
- agent 是建立在 AOS Machine 基礎上的重要擴充，不是 AOS 核心的唯一用途。

## 2. Function、Call 與 Task：後說的內容覆蓋早期簡寫

使用者一開始把 Function 定義為一次 Linux process 呼叫：

```text
argv + stdin -> stdout + stderr + exit status
```

後來又明確補上：資料夾可以是 Function definition；Step、Round 也都是 Function；一個 Function 還能呼叫其他 Functions。因此最新意思不是「所有 Function 永遠等於單一 process」，而是：

- Linux process 呼叫是最底層、最通用的執行形式，也是 LLM Tool 很好的共同底座。
- 一個明確的檔案或資料夾可以承載 Function definition。簡單時是一個檔案，責任變多時可長成資料夾模組。
- 呼叫 agent Function 時，prompt 可以當參數。使用者提出的候選形式是：

```sh
cd bot-a
aos exec ./ask "do something"
```

- 正在執行中的 Function 實例叫 **Task**。
- Function 能呼叫 Function，所以大型工作可以由多個較小工作組成。

使用者沒有親自固定 immutable Call、Receipt、事件格式或 Task 完成後的正式保存語意；那些目前屬於 agent 的延伸設計。

## 3. Step、Tool 與 Round

- Step 與 Round 都是 Function。
- 在 agent 語境中，Step 具有「原子操作」角色。
- Round 是一連串 Step 與 Tool 呼叫組成的 Function。
- Tool 可沿用 Linux process 呼叫形式。較複雜的資料可以再加 JSON、特定檔案格式或額外檔案存取。
- Server API 不必變成新的核心指令；可像 `curl` 一樣由本地程式承接。

「Step 的原子性精確到哪裡」及失敗時如何呈現，使用者尚未固定。

## 4. Agent root 就是 agent 的地盤

- 一個 agent 可以直接由一個資料夾代表，例如 `/home/mine/agents/bot-a`。
- 這個絕對資料夾路徑就是中央 AOS 看見的 agent ID。
- agent root 是該 agent 的地盤；同一地盤內不再放第二個同層 agent 來共同做主。
- agent root 同時很像一個 C Function definition。對 stateful agent 而言，它也會成為執行、記憶與影響自身的場所。
- 因為 agent 會修改自己的地盤，預設應避免同一 agent 同時跑多個實例。
- agent 可成為 team leader；由它決定哪些子資料夾是 child agent 的地盤。`tools/`、`subs/` 等只是推薦布局，不強迫。
- 可在 root 內提供 `./interact`、`./task list` 等入口；互動介面可以是 Python REPL、Janet REPL 或 coding agent。

## 5. Shell、PATH 與 `.aos`

- 最舒服的日常互動應留在 shell 內；bash、zsh 或其他 shell 不是重點，因為主要機制是注入 agent 專用的 `$PATH`。
- coding agent、MCP 等特殊適配不是主線，日後需要時再擴充。
- 使用者偏好 `.aos` 這種做法，但仍要求列出其他方案及優缺點；`.aos` 的精確內容與放置方式尚未裁決。

## 6. Git、改名與模組成長

- 一個 agent root 應能直接上傳 GitHub；不適合進 Git 的內容放進 `.gitignore`。
- agent 自行改名時，推薦由它在自己的 Git repo 內使用 `git mv` 並自行承擔風險。
- 外力改名、symlink、路徑重用屬非常規情況，不進主線；未來 AOS 可另加搬移／改名協助。
- workflow、Tool 或 agent 的責任變多時，可由單檔長成資料夾，再把工作拆成較小模組。這個演進方向應放在較後面的設計路徑。

## 7. Task 與持久化

- Task 本質上可包含 messages、Tool Calls、Tool Results 等內容，因此希望能保存到檔案，像 Lisp image 一樣進入 Git 管理。
- 正在動態執行的 Task 直接落盤很危險；處於 pause 等穩定狀態時保存，比較容易處理。
- AOS 除了排程，也要承擔持久保存責任。filesystem 不只是一般記憶體，還是 AOS 掛掉後仍存在的慢速記憶體。
- agent 的可攜狀態與某台機器上的中央排程狀態必須小心分開；跨機器後不能假裝原機器的執行仍連著。

## 8. 跨機器是遠期方向

使用者說的遠期跨機器包含兩件不同的事：

1. AOS 狀態可透過 Git 在不同機器保留。
2. 兩台正在運作的 AOS 可透過網路交換狀態。

第二項需要檔案同步、server API 或其他同步軟體，明確放在很後面處理。

## 9. 文件、方案與原型方式

- AOS 必須是主軸；`docs/`、FreePy 與舊實作只作 survey／歷史材料。
- 可先完整設計，再交付濃縮版。完整稿放 `full/`，思考過程與被取代內容放 `archived/`。
- 要提出多套具體方案及利弊；共用概念可抽出，共用概念本身也可再列多套方案。
- 由小到大、從基礎到複雜，一步一步設計與驗證。
- 原型先用 Python 快速試，再以 C++／Janet 嘗試固化；Janet 可參考 `C:/code/mine/langlab-janet`。
- 最終文件以繁體中文為主，寫給 C++ 工程師，避免生僻名詞；需要時可用 HTML。

## 10. 今日協作方式

- 可開多個子 agent，各自提出看法，再用回合制交叉審查；主 agent 控制順序與檔案寫入權，避免衝突。
- 架構與設計偏好 Sol；簡單實作、檢查可交 Terra／Luna。
- 主 agent 主要負責接收新想法、排程與裁決；文檔與原型可交給指定子 agent。
- 每做一份規格，可接著做小原型、記錄使用感受，再回頭修正。
- 子 agent 也可以親自試用原型，不能只看程式碼猜使用感受。
- 使用者上班時要降低認知負擔；可選審閱放 `wait_user/`，以約 2–5 分鐘、短而好懂為原則，不阻塞其他工作。
- 2026-08-14 當天可持續工作到使用者約 17:00 下班前，不必為了立即交稿跳過試作與修訂。
- Claude Desktop／Opus 可作少數幾輪外部審查；材料放在 `simple_tools` 下的獨立資料夾並附 README。
