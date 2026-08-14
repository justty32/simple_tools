# AgentOS 操作介面：把關版

這是目前提案的最短版本。Python prototype 還沒換成這套介面；先確認用法，再一小步一小步實作。

## 只要記住三件事

```text
AgentOS    apt 安裝的共用程式
Bot        一個目錄物件：LLM + messages + tools
Run        這個 Bot 目前正在做的工作
```

**已確認：一個 Bot 同時間只有一個 active Run。** 所以沒有 Run ID，`./run` 永遠控制眼前這份工作；
不同 Bot 仍可同時跑。

## Bot 長這樣

```text
bot-a/
  start       開始工作
  status      看狀態
  run         控制目前工作
  llm         看／改模型
  messages    看／改對話
  tools       看／改工具
  .agentos/   private data
```

這些公開檔都是很薄的 executable。真正程式由 apt 安裝；未來從 Python 換成 C++／Janet，既有 Bot 與
shell command 都不用改。

## 日常用法

```sh
sudo apt install agentos
agentos new bot-a

./bot-a/llm set model qwen3.5:9b
./bot-a/llm set temperature 0.1
./bot-a/start "do something"
```

長工作放背景：

```sh
./bot-a/start -b "inspect this project"
./bot-a/status
./bot-a/run wait
```

逐步把關：

```sh
./bot-a/start --step "fix the bug"
./bot-a/run next       # 一次 LLM，或一個 tool
./bot-a/status
./bot-a/run next
./bot-a/run continue   # 不想逐步了，改回自動
```

## `run` 只用簡單動詞

| command | 意思 |
|---|---|
| `next` | 完整做下一件事，然後停下 |
| `pause` | 當前 LLM/tool 完成後暫停 |
| `continue` | 改回自動執行 |
| `send TEXT` | 回答 Bot；paused 且沒有 pending tool 時可追加要求 |
| `skip REASON` | 不執行眼前的 tool，並留下原因 |
| `stop` | 在安全位置永久停止 |
| `wait` | 等到 terminal state |
| `answer` | 只輸出最近 assistant 文字 |
| `log` | 看已保存紀錄 |

一次 `next` 就是一條 Agent Machine CPU instruction：一次 LLM call 或一個 tool call。模型要求三個 tools，
就是三次 `next`，因此每個 tool 之間都能檢查或 stop。

看狀態只用 `./status`；`./run` 沒參數時顯示 help。預設最多 64 次 call，真的碰到才用 `run more N`，不把
少用的安全開關塞進日常流程。

## 狀態不玩文字遊戲

```text
idle ready working waiting paused done stopped failed
```

- `working` 另顯示 `doing: llm | tool`。
- `waiting` 只表示 Bot 明確使用 `ask_user` 等你回答。
- 普通 tool 在逐步模式是 `paused + next: tool`，不混成 waiting。
- `after: pause | stop` 表示要求已接受，正等眼前 instruction 完整收尾。
- tool 已確定失敗仍可交給 LLM 修正；只有 outcome unknown 或核心狀態壞掉才讓整個 Run failed。

## 三個 Bot 成員

```sh
./llm                         # effective JSON
./llm raw                     # 保留 $ref/$env
./llm use ../shared/ollama.json
./llm set max_tokens 4096
./llm unload                   # 明確釋放目前 local model

./messages
./messages set system "Reply briefly."
./messages clear              # 清對話，保留 system

./tools
./tools add ../shared/read-tool.json
./tools remove read_file
```

所有修改都只改 Bot private manifest，不修改 `$ref` 來源，也不把它展平成一大份 JSON。messages 與 tool
call/result 嚴格配對；tool call 使用的小型 `call_id` 只是收據號碼，不是檔案、Bot 或 Run identity。

切換 direct Ollama model 時，`llm set model` 回成功以前會先安全 unload 舊模型。其他 AgentOS Bot 正在用
同一模型時會等它結束，不能誤卸載；不支援 unload 的 remote endpoint 會明說 not supported。

## Handle 與 Controller 不增加使用者負擔

- Handle = `.agentos/` 裡的 state、messages、journal；不是活著的 Python object。
- Controller = internal detached worker；不是另一個公開物件或 command。
- 同一 Bot 一把 executor lock；auto 與 `next` 不會重複做同一件事。
- pause／waiting 不占 parked thread。Ctrl-C 只離開等待畫面，不殺背景 instruction。
- crash 後只有確定未送出的工作可重試；已送出但結果不明就 fail closed。
- Python v1 先靠 Linux 排程；跨 Bot token／確定性／foreground UX 排程留給未來 C++ per-user service。

## 目前唯一需要你定案的語意

提案：LLM 回普通文字而沒有 tool calls，就視為這次 Run `done`。只有明確呼叫 `ask_user` 才 waiting。
之後再 `start "more"` 仍沿用 Bot messages，所以對話不會斷。這比分析問號或猜模型語氣更 KISS、可測。

精確狀態表見 [`COMMANDS.md`](COMMANDS.md)；使用場景見 [`SCENARIOS.md`](SCENARIOS.md)；private storage、
messages pairing 與 runtime 分別見 [`STORAGE.md`](STORAGE.md)、[`MESSAGES.md`](MESSAGES.md)、
[`RUNTIME.md`](RUNTIME.md)。
