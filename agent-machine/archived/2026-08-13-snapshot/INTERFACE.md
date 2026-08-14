# AgentOS 操作介面候選

本文件是待確認的目標介面，不代表目前 Python prototype 已全部實作。原則是 KISS、簡單英文、
subcommand 優先，以及日後換成 C++／Janet 時不改使用方式。各指令在不同狀態下的精確行為見
[`COMMANDS.md`](COMMANDS.md)，`$ref`／`$env` 規則見 [`CONFIG.md`](CONFIG.md)，shell 輸出規則見
[`OUTPUT.md`](OUTPUT.md)，Handle／Controller 的 Linux runtime 契約見 [`RUNTIME.md`](RUNTIME.md)。
messages 與 tool-call 配對規則見 [`MESSAGES.md`](MESSAGES.md)。
LLM、messages、tools 的最小資料形狀見 [`MEMBERS.md`](MEMBERS.md)。
Run 的安全上限見 [`LIMITS.md`](LIMITS.md)。
三個 private raw manifest 與 effective composition 見 [`STORAGE.md`](STORAGE.md)。
Linux exec tool 的完整 recipe 見 [`EXEC.md`](EXEC.md)。
tool JSON 的 strict shape 見 [`TOOL_SPEC.md`](TOOL_SPEC.md)。

## 三層模型

```text
AgentOS        安裝在系統中的 runtime 與工具
Bot            一個目錄物件，擁有 LLM、messages、tools
Run            Bot 目前正在做的一次工作
```

FreePy 名詞只留在內部：

| FreePy | AgentOS 對外 |
|---|---|
| Round | Run，但平常只說「目前工作」 |
| Step / advance | `run next` |
| Handle | `.agentos/` 內的私有 run state |
| Controller | AgentOS runtime，不公開這個名詞 |

**已確認：v1 每個 Bot 同時只准一個 active Run。** 這符合現有 Bot 只有一份可變 messages 的模型，也省去
Run ID、current pointer 與 history merge。不同 Bot 可以同時工作。若日後真的需要同 Bot 平行 Run，
再把 Run 升成公開物件，不提前增加複雜度。

## 安裝與物件外形

```sh
sudo apt install agentos
agentos new bot-a
```

```text
bot-a/
  start       開始工作
  status      看 Bot 與目前工作
  run         控制目前工作
  llm         查看／修改 LLM
  messages    查看／修改 messages
  tools       查看／修改 tools
  .agentos/   私有資料、狀態、紀錄
```

公開成員都是薄 executable；重型程式在系統安裝目錄。`.` 只表示 private 慣例，不是安全機制。

## 最常用的命令

```sh
./bot-a/start "do something"       # 前景執行，stdout 最後只放答案
./bot-a/start -b "long work"       # 背景執行，立刻返回
./bot-a/start --background "work"  # -b 的完整寫法
./bot-a/start --step "check this"  # 逐步模式，不自動往下跑

./bot-a/status
./bot-a/status --json

./bot-a/run next
./bot-a/run pause
./bot-a/run continue
./bot-a/run send "also check tests"
./bot-a/run skip "do not run this tool"
./bot-a/run stop
./bot-a/run wait
./bot-a/run answer
./bot-a/run log
```

`start` 遇到 active Run 時拒絕，避免控制錯工作。完成的紀錄可保留在 private archive；新 `start`
會建立新的目前工作。

## `run` subcommands

| 命令 | 意思 |
|---|---|
| `next` | 完整做下一件事，然後停下 |
| `pause` | 目前正在做的事完成後暫停；預設等到真的停下 |
| `continue` | 從暫停處自動繼續 |
| `send TEXT` | 回答 waiting；paused 且沒有 pending call 時只保存 |
| `skip REASON` | 不執行下一個 pending tool，留下被使用者略過的結果 |
| `stop` | 在安全位置永久停止，不能 continue |
| `wait` | 等到結束；成功時輸出最後答案 |
| `answer` | 只輸出最新 assistant 文字，方便 pipe |
| `log` | 顯示已保存的執行紀錄 |

看狀態一律用 `./status`。`./run` 沒有 subcommand 時顯示短 help；不再提供同義的 `run status`。少用的
call 安全上限與 `run more` 收在 [`LIMITS.md`](LIMITS.md)，不占日常命令清單。

`next` 不使用 LLM 術語：它只表示「下一件完整工作」。依 Agent Machine 的 CPU 模型，一次 `next`
只做一次 LLM call 或一次 tool call。若模型要求三個工具，就是三次 `next`；這樣每個工具之間都能
檢查、pause 或 stop。

`pause`／`stop` 不切斷已開始的 LLM 或 tool。當前工作先完成並留下結果，再停止往下。已發生的
外部副作用不假裝 rollback。未來若需要強制殺 process，另用明確的 `kill`，不混入 `stop`。

`send` 只有兩種合法位置：waiting 時回答 `ask_user`；paused 且沒有 pending tool call 時追加要求並保持
paused。若 paused 仍欠 tool result，先 `next` 或 `skip`，不能把 user message 插進配對中間。

## 簡單狀態

公開狀態只需要：

```text
idle ready working waiting paused done stopped failed
```

細節用欄位表示，不增加大量狀態名：

```json
{
  "status": "working",
  "doing": "llm",
  "after": "pause"
}
```

`after: pause` 表示 pause 已接受，但目前的完整工作尚未結束。`waiting` 只有一個意思：Bot 透過
`ask_user` 明確等待回答。普通 tool 若需要逐步檢查或批准，狀態是 `paused`，並顯示 `next: tool`。

v1 提案是：LLM 沒有要求 tool、只回普通文字時直接 `done`；只有明確呼叫內建 `ask_user` tool 才進入
`waiting`。因 messages 屬於 Bot，使用者之後再 `start "more"` 仍可延續同一段對話。

## Handle 與 Controller 放在內部

不公開 Handle ID，也不回傳 Run path。Handle 的作用由 `.agentos/` 內「目前工作」的 state 與 log 取代；
process 結束後仍可讀回，不依賴活著的 Python object。

Controller 只是 AgentOS runtime 的內部責任：

- 同一個 Bot 同時只准一個 writer／executor。
- 自動執行與 `run next` 不會同時領到同一件工作。
- 其他 process 的 `pause`、`send`、`stop` 會排入同一條控制順序。
- 每條 LLM／tool instruction 完整保存後，才公開新狀態。
- `waiting`／`paused` 時不占用一條 parked thread。
- runtime 重啟時從 private state 與 log 恢復，不從 memory 猜測。

runtime 最初可以是按需啟動的 Python process，日後也可以是 C++ service；這是內部選擇，不改公開命令。

## LLM、messages、tools

不帶 subcommand 時輸出 effective JSON：

```sh
./llm
./messages
./tools
```

共同的簡單 subcommands：

```sh
./llm raw
./llm check
./llm use ../shared/ollama.json
./llm set temperature 0.1
./llm unset temperature
./llm unload

./messages raw
./messages check
./messages use ../shared/system-message.json
./messages set system "use Traditional Chinese"
./messages unset system
./messages clear                  # 清對話，保留 system
./messages clear --all            # system 也清掉

./tools raw
./tools check
./tools add ./read-tool.json
./tools remove read_file
```

- `raw` 保留 `$ref`／`$env`；無參數輸出解析後的 view。
- `check` 檢查 JSON、schema、缺少的 env、ref loop 與 tool schema/executor 配對。
- `set` 依 schema 解析型別，所以 `0.1` 是 number，不是字串。
- `set` 寫 Bot 自己的 local override，絕不偷偷修改 `$ref` 指向的共用檔。
- secret 的 effective view 預設遮蔽；`raw` 只顯示 env 名稱。
- messages 中的 system 只是第一則 system message，不另造第四個 Bot 成員。

設定只可在沒有 active Run、`paused` 或 `waiting` 時修改；`ready`／`working` 時會要求先
`run pause`。修改在下一件工作生效。成員自己的安全限制仍優先：waiting 有 pending `ask_user`，因此不能
clear messages 或修改 system。
移除 pending tool 所需工具、清除相關 messages 或切換 model 時都拒絕；先 `next` 完成或 `skip`。沒有
pending tool 後，切換 model command 才安全 unload provider 支援的舊模型，再保存新設定；新模型在下一次
LLM call 載入。

## 穩定邊界

公開契約是 Linux filesystem、argv、stdout/stderr、exit code、JSON/JSONL；不是 Python class。
Bot 的薄腳本最後都呼叫同一個 `agentos` binary。Python prototype、C++ core 與 Janet/Lisp 行為層都必須
通過相同的 subprocess 測試與資料格式，使用者命令不隨實作語言改變。
