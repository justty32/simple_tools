# AgentOS shell 輸出契約

公開介面同時要適合人直接使用，也要能被 shell script、Python、C++ 與 Janet/Lisp 穩定呼叫。

## 預設輸出

- stdout 只放呼叫者真正要取走的資料。
- stderr 放進度、簡短確認與錯誤說明。
- UTF-8、Linux newline；有輸出時最後留一個 newline。
- 進度顏色只可在 stderr 是 TTY 時使用。

| 命令 | stdout | stderr |
|---|---|---|
| `start`／`run wait` 成功 | 最後答案 | 進度 |
| `run answer` | 最近一則 assistant 文字 | 無或錯誤 |
| `llm`／`messages`／`tools` | effective JSON | 錯誤 |
| `raw` | raw JSON | 錯誤 |
| `status`／`run log` | 人類可讀內容 | 錯誤 |
| `pause`／`continue`／`send`／`skip`／`more`／`stop` | 空 | 簡短確認或錯誤 |
| `run next` | 空 | 本次做了什麼 |
| `start -b` | 空 | started 或錯誤 |

`start -b` 只有在 runtime 已持久接受工作後才回 0。因為一個 Bot 只有一個 active Run，不必輸出 ID
或 path；後續直接用 `./status` 與 `./run ...`。

前景 `start` 與 `run wait` 有 controlling TTY 時，遇到 `ask_user` 會把問題印到 stderr、從 `/dev/tty`
讀一行，再等下一個狀態；stdout 仍只留最後答案。沒有 TTY 時不讀 stdin，也不猜答案，只保持等待，讓
另一個 process 使用 `run send`。

## `--json` machine mode

需要穩定資料時使用 `--json`：

```sh
./status --json
./run next --json
./run stop --json
./run log --json
```

- `status` 與控制命令恰好輸出一個 JSON object 到 stdout。
- `run log --json` 輸出 JSONL，一行一個完整 event。
- 預期中的拒絕也輸出 JSON，並回 non-zero；不能先印半個成功 object 再失敗。
- machine mode 不印一般進度。連 command line 都無法解析的 fatal error 才可直接寫 stderr。
- 未知的新欄位必須忽略；改變既有欄位意思時必須升 format version。

設定 view 本來就是 domain JSON，不再多包一層 command envelope，因此 `messages`／`tools` 可以直接是
JSON array。它們的 `$ref`／`$env` 語意由 [`CONFIG.md`](CONFIG.md) 固定，LLM、message、tool 的 schema
各自版本化。

v1 的 `start`、`run wait`、`run answer` 是 text commands，不接受 `--json`；需要 machine state 時另呼叫
`status --json`。`llm/messages/tools` 本來就輸出 JSON，也不需要 `--json`。`check` 與修改命令可加
`--json`，使用 `agentos.command.v1` envelope。

## 最小 JSON objects

狀態至少包含：

```json
{
  "format": "agentos.status.v1",
  "status": "working",
  "doing": "tool",
  "after": "pause"
}
```

`status` 是必要欄位。`doing`、`after`、`need`、`next`、`mode` 只在有意義時出現，不用 `null` 填滿畫面。

控制命令成功：

```json
{
  "format": "agentos.command.v1",
  "ok": true,
  "command": "pause",
  "status": "paused"
}
```

預期中的拒絕：

```json
{
  "format": "agentos.command.v1",
  "ok": false,
  "command": "send",
  "error": {
    "code": "pause_first",
    "message": "pause first"
  }
}
```

`message` 給人看，可以改好懂的措辭；程式只比較穩定、簡短的 `code`。

每一行 event 至少有遞增 `seq` 與 `kind`。一條完成的 CPU instruction 例如：

```json
{
  "format": "agentos.event.v1",
  "seq": 18,
  "kind": "instruction",
  "instruction": 5,
  "executor": "tool",
  "phase": "finished",
  "outcome": "done"
}
```

v1 journal 的 `kind` 只有：

| kind | 用途 |
|---|---|
| `command` | start、pause、continue、send、skip、more、stop 等已接受操作 |
| `instruction` | LLM/tool 的 prepared、dispatching、finished |
| `tool_result` | send 回答、skip 或 terminal pairing closure 產生的 result |
| `state` | ready、waiting、paused、terminal 等正式轉移 |
| `recovery` | crash 後 replay、commit 或 unknown 判定 |

`executor` 是 `llm` 或 `tool`；`outcome` 至少有 `done`、`failed`、`skipped`、`not_run`、`waiting`、
`unknown`。同一 instruction 的 journal events 共用遞增的 private instruction number；它不是公開 Run/Node
identity。特定 LLM／tool 的資料放在額外欄位，secret 預設遮蔽。

`seq` 是同一 Bot journal 的唯一順序，從 1 開始且不可重複或倒退。`at` timestamp 可以附加，但 replay
不得依賴時鐘排序。unknown kind fail closed；同一 kind 的未知額外欄位可忽略。

## exit code

v1 只固定少量常見值：

| code | 意思 |
|---|---|
| 0 | 命令達成 |
| 1 | 命令有效，但狀態拒絕、設定無效或工作失敗 |
| 2 | command line 用法錯誤 |
| 130 | 使用者以 Ctrl-C 中斷這個等待 client |

Ctrl-C 只讓前景 client 回 130；已被 runtime 接受的工作、pause 或 stop request 不因此撤銷。

`run wait` 只在 terminal state 返回。done 時輸出答案並回 0；stopped／failed 時 stdout 為空，錯誤走
stderr 並回 1。waiting 時顯示所需動作並繼續等待。`run answer` 尚無 assistant 文字時也輸出空 stdout
並回 1。
