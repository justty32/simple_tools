# Shell、CLI 與 PATH

使用介面的目標是讓 AOS 像普通 Unix 工具：人在 Agent root 裡工作，仍留在熟悉的 bash／zsh 等 shell；AOS 只提供這個地盤需要的命令與狀態。

本頁的命令都是**候選示意**，不是已定 CLI。

## 最短心智模型

### 使用者偏好

```text
cd /home/mine/agents/bot-a
./interact
```

進入後仍是 shell，只是 `$PATH` 前面加入 `bot-a` 專用操作。使用哪種 shell 不重要；重要的是不強迫使用者離開熟悉環境，也不讓每個命令都重打 Agent path。

另一個直接呼叫的候選是：

```text
cd bot-a
aos exec ./ask "do something"
```

這可解讀為「在目前 Agent 地盤，請 AOS 接受並執行 `./ask` 代表的 Function Call」。`aos exec` 應是通用 Function → Task 入口，不把 Step 或 Round 寫死；`./ask` 自己若是 Round Function，才建立相應 Task tree。

## `interact` 能做什麼

普通 `./interact` 子 process 無法修改 parent shell 的環境。**目前推薦的原型**是啟動同一終端內的 nested interactive shell，為它設定：

- 已驗證的 Agent root identity。
- 指向該 root 專用 commands 的 `$PATH` 前綴。
- 查詢中央 AOS 的入口與本次 Runtime 啟動資訊。

它不應只靠目前 `cwd` 反覆向上找 `.aos`；進入時固定 root context，之後在 descendants 間 `cd` 也不改 Agent ID。

其他方案包括讓使用者 `source` 一段 shell function、由 directory hook 自動切換，或由外層 launcher 開專用 shell。它們各有便利與驚訝副作用，留在 options 比較。

## PATH 只提供入口，不提供權威

PATH 中可有易記的 wrapper，例如 `task`、`call` 或 Agent 自有 tools；但 wrapper 只把「我是從哪個明確 root 啟動」交給安裝好的 AOS client。

安全與狀態檢查仍由 client／AOS 完成：

- 驗證 root 已明確登記，不把普通 directory 當 Agent。
- 將相對 Function path 依明確 root／cwd 規則解析。
- 不用 shell 拼接 argv。
- 向中央查 active Tasks、寫入資格與 AOS 狀態。
- 任何正式狀態轉移仍走 AOS，不靠直接覆寫顯示檔。

環境變數可攜帶不變的身分與查詢入口，但 active set 會在 process 啟動後改變，不能塞一份清單到 env 就當即時真相。秘密與正式寫入 capability 也不應透過會被所有 child 繼承的普通 env 傳遞。

## 日常層與進階層

**目前推薦**：把介面分成兩層。

### 日常 Agent 操作

- 呼叫目前 Agent。
- 追加 prompt 到目前 Task。
- 明確建立帶開始條件的排隊 Task。
- 一眼查看 Agent 是否忙、目前 Task tree 與 AOS 是否在線。

普通呼叫遇 busy 必須直接說明「Agent 正忙」，而且零 Task；不能靜默排隊。追加與排隊應是不同、明確的動作。

### 進階 AOS 操作

- 檢查 Task tree、Function Call、Return 與結果不明原因。
- pause、stop、repair、checkpoint、attach／detach。
- 查看 queue、executor、容量與恢復狀態。
- 直接提交非 Agent Function，供除錯、bootstrap 或一般工作使用。

使用者不應為了日常呼叫先理解內部 attempt、hash 或 event；需要診斷時才展開。

## `task list` 要回答什麼

### 使用者已定

從 Agent root 應能看到目前 Tasks；若同 root 有多個 active Tasks，Agent 本身也要知情。

**目前推薦**：簡短畫面至少分開：

- Agent root identity 與 attach／detached。
- AOS 是 ready、recovering、draining、offline，還是只顯示 cached 狀態。
- 哪一棵 tree 持有 root 寫入資格。
- 其他 active Tasks、等條件者、條件已滿但等容量者。
- 是否存在 unknown、待修復或尚未 checkpoint 的狀態。

不要只顯示模糊的 `idle`／`busy`。Folder 內顯示可以讓 Agent 讀，但即時命令仍向中央 AOS 確認。

## 人類輸出與機器輸出

**目前推薦**：同一 client 可有兩種呈現，但共享同一驗證與語意實作。

- 人類模式用繁中／可讀文字、穩定欄位順序與下一步提示。
- 機器模式使用有版本、有界且嚴格驗證的資料；stdout 專供資料，stderr 專供診斷。

機器格式尚未固定，不應先把原型 JSON 公開成 ABI。錯誤輸出至少要能分清：普通拒絕、結果不明、資料矛盾、AOS 離線與使用方式錯誤。

## Foreground 與 detach

Task 是否由 terminal 等待，只是 client 是否 attach，不應產生兩種不同 Task。可用 foreground 顯示進度，也可明確 detach 後稍後查詢；名稱與旗標仍待選。

detach 不等於停止，checkpoint 也不等於 Git commit。跨機搬運前要先到可攜安全狀態，再 checkpoint、detach、由使用者執行 Git；clone 後不自動 resume。

## 路徑與直接執行

相對 Function path 應在接受前依明確 context 解析，保存後不再因 client 後來 `cd` 而改義。Definition bytes 與它讀取的 filesystem memory 是否 live、checked 或 snapshot，是另一個選擇，不能只靠絕對 path 假裝已凍結。

直接執行 `./ask` 可以保留作開發與除錯，但它繞過 AOS 時沒有 Task 保存、排程、結果不明保護或 checkpoint 保證。正式路徑應明確經過 AOS；文件與提示要讓兩者差異可見。

## 尚未決定

- 最終命令名稱是 `aos exec`、`aos run`、Agent 專用 `call`，或其組合。
- `./interact`、`./task` 是 tracked wrapper、產生檔還是安裝工具的別名。
- nested shell、`source`、shell function 與 directory hook 的預設方案。
- prompt 如何安全輸入 raw bytes、檔案或多行內容。
- checkpoint／detach／Git 的便利 alias 是否值得提供。

原型應先用兩至五分鐘的日常流程測認知負擔，再固定 CLI 拼法。
