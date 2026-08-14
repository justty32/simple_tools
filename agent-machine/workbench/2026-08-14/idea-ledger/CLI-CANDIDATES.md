# CLI 操作方式候選

> 本頁整理 `exec_surface_round2` 支線的想法。全部仍是候選，不是已定規格。

## 先固定內部流程

不論外面長什麼樣，建議都先降成同一條流程：

```text
Definition path + argv + stdin + cwd／環境規則
    -> Call
    -> AOS 接受並保存
    -> Task reference
    -> 命令列端（client）可選擇等待結果
```

這裡有三種不同身分，公開介面不應都叫 `root`：

- `agent_path`：agent 地盤的絕對路徑，例如 `/home/mine/agents/bot-a`。
- `definition_path`：這次要呼叫的 Function，例如 `/home/mine/agents/bot-a/ask`。
- `task_id`：AOS 接受這次 Call 後建立的工作身分。

Function 的 `cwd` 也不能只靠 AOS manager 當時在哪個目錄。client 應先把目標解析清楚，再把 `cwd` 與環境採用規則放進 Call。

## 方案一：完全明示

```sh
aos exec --agent /home/mine/agents/bot-a -- \
  /home/mine/agents/bot-a/ask "do something"
```

`--` 後第一項是 Definition，後面全部原樣成為 Function 參數。這個形式主要作語意基準與測試介面。

優點：

- agent、Function 與參數的邊界最清楚。
- 不受目前目錄或 `$PATH` 順序影響。
- 適合重現錯誤、寫測試與機器呼叫。

缺點：

- 日常輸入太長。
- 使用者需要先知道 agent 的絕對路徑。
- 若每次都要填 `cwd`、環境規則，會更笨重。

建議：保留成所有方便介面的共同基準，不作主要日常寫法。

## 方案二：在 agent 地盤內呼叫

```sh
cd /home/mine/agents/bot-a
aos exec ./ask "do something"
```

候選規則是：目前目錄必須有明確的 agent 宣告；AOS 不一路往父目錄猜。client 先把 `./ask` 轉成絕對 Definition path，再建立 Call。

優點：

- 最接近使用者偏好的操作方式。
- 短、好記，也保留「我正在 bot-a 地盤內工作」的感覺。
- 同一 repo clone 後，只需在新位置重新連到當地 AOS。

缺點：

- 尚需決定「這裡是 agent root」由哪個檔案明示。
- Definition 從單檔長成同名資料夾後，入口規則仍待決定。
- 必須分開處理 caller 目前目錄、agent root 與 Function `cwd`。

建議：作日常主介面；先由方案一驗證的同一條內部流程支撐。

## 方案三：用 PATH 提供短命令

進入 agent 地盤後，把一組 AOS 控制用的小程式放進 `$PATH`：

```sh
task list
task wait TASK_ID
interact
```

這些命令只是 client 代理，最後仍呼叫 AOS。真正的 `ask`、Tool 或其他 Function 不直接當成普通 PATH executable，否則 shell 可能繞過 AOS，沒有建立可保存的 Task。

優點：

- 留在原本 shell 內，操作最輕。
- 不限定 bash、zsh 或其他 shell。
- 可以讓每個 agent 提供自己的輔助操作。

缺點：

- 兩個 agent 有同名命令時，容易被舊 PATH 順序送錯地方。
- PATH 注入與退出 agent 地盤時必須可靠還原。
- 代理程式若另做一套解析規則，會和 `aos exec` 分歧。

建議：只加 Task／互動控制代理；所有執行仍降成方案一的 Call。

## 方案四：`./interact` 開 agent 子 shell

```sh
cd /home/mine/agents/bot-a
./interact
```

它可設定 prompt、PATH 與目前 agent，再啟動使用者原本的 shell。候選定位只是 client 介面，不自行保存另一份 Task 真相。

優點：

- 可在 prompt 上清楚顯示目前 agent。
- PATH 的進入與離開有明確邊界。
- 日後能接 Python／Janet REPL 或 coding agent。

缺點：

- shell 巢狀後，使用者可能忘記自己在哪一層。
- TTY、遠端連線與中斷行為比非互動 Call 複雜。
- 太早做會混入尚未設計的即時互動與串流問題。

建議：等 Task wait／detach 與 agent attach 穩定後再做。

## 四套方案共用的行為候選

- 前景 `aos exec`：先持久接受 Call，再由 client 順便等待同一 Task。
- `--detach`：只是不等待；它建立的 Task 與前景模式相同。
- Ctrl-C：client 結束等待並回傳 130，不代表停止 Task。
- 再打一遍 `aos exec`：建立新 Task，不是重新連回舊工作。
- 回到舊工作：使用 `aos task wait TASK_ID` 或同義命令。
- `aos agent attach AGENT_PATH` 與 `aos task wait TASK_ID` 分開命名，避免兩種 attach 混在一起。
- 人類模式可把 Task reference 寫到 stderr；機器模式回固定 JSON。前景 stdout 留給 Function Return。
- 第一版 `exec` 只處理非互動 durable Call；PTY／REPL 另行設計。

這些仍需原型驗證，尤其 CLI exit code 不能簡單等同 leaf process 的 exit status。

## 建議順序

1. 用方案一固定 parser、Call 與 Task reference。
2. 加方案二，作為主要日常 UX。
3. 加方案三的控制代理，但不讓 Function 繞過 AOS。
4. 最後才試方案四的互動子 shell。

## 尚待原型

- `--` 前後的參數、以 `-` 開頭的 prompt，是否原樣傳入。
- 從不同目錄呼叫同一 Definition，是否得到明確且可重現的 `cwd`。
- Ctrl-C、terminal 關閉後，Task 是否仍在且只執行一次。
- foreground、`--detach` 與 JSON 模式的 stdout／stderr／exit code。
- 兩個 agent 都有同名 PATH 代理時，是否會送錯 agent。
- `ask` 由單檔長成 `ask/` 後，原命令能否在不猜 `main.py` 的情況下演進。
- agent 忙碌時，第二次 `exec` 是排隊或拒絕；這要和並行方案一起測。
