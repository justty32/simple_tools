# AgentOS messages 契約

messages 是 Bot 的三個成員之一，也是 LLM 與 tools 之間的正式對話紀錄。本頁固定最小 JSON 形狀；
各 endpoint adapter 再轉成 OpenAI、Ollama 或其他 wire format。

## 四種 message

```json
{"role":"system", "content":"Reply in Traditional Chinese."}
```

```json
{"role":"user", "content":"Read README."}
```

```json
{
  "role":"assistant",
  "content":"I will inspect it.",
  "calls":[
    {"call_id":"call-1", "name":"read_file", "arguments":{"path":"README.md"}}
  ]
}
```

```json
{
  "role":"tool",
  "call_id":"call-1",
  "content":"...file text..."
}
```

- system／user 固定只有 `role, content`。system 最多一則且只能放第一則。
- assistant 固定有 `role, content, calls`；tool 固定有 `role, call_id, content`。未知 key fail closed。
- v1 的 `content` 是 UTF-8 string。圖片與其他 content parts 之後另升 schema，不在 v1 猜格式。
- assistant 可以同時有普通文字與 calls；兩者不能因 adapter 方便而互相覆蓋。它至少要有非空 content
  或非空 calls；沒有的那一項分別用 `""` 或 `[]`，不使用 null。
- reasoning 不放 messages；受控 event 也預設不保存敏感 chain-of-thought。
- 不把 Python object、exception 或 provider SDK object 放入 messages。

正常 call 使用 parsed JSON object：

```json
{"call_id":"call-1", "name":"read_file", "arguments":{"path":"README.md"}}
```

若 endpoint 回傳被 max token 截斷或不合法的 JSON，不能把它假裝成空 object：

```json
{"call_id":"call-2", "name":"write_file", "raw_arguments":"{\"path\":"}
```

`arguments` 與 `raw_arguments` 必須恰好有一個。raw form 絕不 dispatch；下一條 tool instruction 只追加
配對的錯誤 result，讓模型可修正，也保留 endpoint 真正回了什麼。

## `call_id` 只是配對號碼

`call_id` 不等於 node ID、Bot ID 或 Run ID。它只在 messages 中回答：

```text
這一則 tool result 是在回答哪一個 tool call？
```

adapter 優先保存 endpoint 給的 call ID；若 endpoint 沒給，runtime 產生目前 pending group 內不重複的
簡短值。它一旦寫入 assistant message就不可改。每個 tool result 必須帶相同 `call_id`。

## 多個 tools 的順序

一則 assistant message 可以提出多個 calls。v1 依 calls array 的順序逐一處理，讓 replay 與人類逐步
檢查都很直白：

```text
assistant calls [A, B, C]
tool result A
tool result B
tool result C
下一次 LLM
```

- assistant message 必須一次完整提交，不能先寫 A、稍後再補 B。
- 每個 call 恰好有一個 tool result；重複、未知或順序錯誤的 `call_id` 都拒絕。
- pending call 就是第一個尚未有 result 的 call。
- pending calls 尚未清完前，不准插入 system、user 或另一則 assistant message。
- `run next`／`run skip` 每次只解決第一個 pending call。
- 手動執行一個後仍為 paused：若還有 calls，`next` 指向下一個 tool；全部完成後，`next` 指向 LLM。
- automatic worker 在每個完整 result 後重新讀 control state；沒被 pause／stop 才繼續。
- active Run 可以暫時停在 assistant 加前幾則 result 的合法 prefix；idle／terminal messages 必須全部配完。

## tool outcome

tool message 本身只保存模型真正會看見的 `content`；結構化 outcome 放在同一次 instruction event，使用
少量固定值：

| value | 意思 | 本 Run 會否再自動呼叫 LLM |
|---|---|---|
| `done` | tool 正常回傳 | 可以 |
| `failed` | 確定失敗，content 是有界錯誤 | 可以 |
| `skipped` | 使用者拒絕，content 是理由 | 可以 |
| `waiting` | `ask_user` 已執行，還沒有答案 | 不會，先 `send` |
| `not_run` | terminal 前補齊未取得真實結果的 call | 不會，本 Run terminal |
| `unknown` | 已 dispatch，但不確定副作用是否發生 | 不會，本 Run failed |

`ask_user` instruction 可先記 `outcome: waiting`；這時尚未追加 tool message。`send` 到達後才以另一則
control/result event 記 `done`，並追加配對內容。

`failed` 仍是合法 tool result，讓模型可以改參數或改走別條路。`unknown` 只保存事實，不假裝成普通失敗
讓模型自動重試；Run 進入 failed，交由使用者看 log。

`skip REASON` 不執行 tool，但追加一則 tool result，event 使用 `outcome: skipped`，content 使用簡單文字，例如
`User skipped this tool: REASON`。因此 pairing 仍完整，而且下一次 LLM 知道工具為何沒有結果。

進 terminal 前，未開始的 pending calls 補 `Not run: ...`；結果不明的當前 call 補
`Error: outcome unknown; not retried.`。依序 append `tool_result` event：前者是 `not_run`，後者是 `unknown`。
這些 closure 不算 instruction、不呼叫 LLM；全部落盤後才發布 terminal。

Run 在 waiting 時，`stop` 替 `ask_user` 追加 `Not answered: run stopped by user.`；致命失敗則追加
`Not answered: run failed before the user replied.`，event 都是 `not_run`。`ask_user` 先前已算
一條 instruction；這個 terminal 補件不再計一次，也不把文字送回 LLM。

`not_run`／`unknown` 是 event 判定，不是第五種 message。配對後仍是合法 history；本 Run 不再呼叫 LLM，
但日後明確 `start` 時模型會看到 closure 文字。AgentOS 不截斷 history，也不偷重試原 tool。

## 明確詢問使用者

v1 不從普通文字猜「模型是不是在問問題」。普通 assistant text 且沒有 calls，就代表本次 Run `done`。
下一次 `start "more"` 仍沿用 Bot messages，所以對話不會消失。

需要在同一次 Run 等人回答時，使用一個明確的內建 tool：

```json
{
  "call_id":"call-2",
  "name":"ask_user",
  "arguments":{"question":"Which file should I read?"}
}
```

runtime 執行下一個 `ask_user` call 時使用內建 executor，不啟動外部 process；這仍算一條 tool instruction，
完成的 state effect 是進入 waiting 並顯示：

```json
{"status":"waiting", "need":"message", "prompt":"Which file should I read?"}
```

它的 schema 固定要求恰好一個非空 `question` string。arguments 不合格時不顯示空問題、不進 waiting；runtime
追加一則有界的 `failed` tool result，和其他可確認的 tool error 一樣處理。

auto mode 會自動執行這條 instruction；step mode 要一次 `run next`。之後 `run send "README.md"` 會追加
一則對應 `call-2` 的 tool result：

```json
{
  "role":"tool",
  "call_id":"call-2",
  "content":"README.md"
}
```

auto mode 收到回答後繼續；step mode 則轉成 paused。paused 且沒有 pending call 時，`run send TEXT` 才
追加普通 user message，並保持 paused。如此不會把 user message 插在 assistant tool call 與 result 中間。

## commit 與 Bot history

一次 LLM call 的 assistant message、一次 tool call 的 result，都是各自不可拆的 commit。result 落盤後
才算 call 已清償。Run done 後 messages 留在 Bot，下一個 Run 繼續使用；`messages clear` 只能在沒有
active Run 且沒有 pending call 時做。

每次送 endpoint 前做完整 validation。OpenAI-compatible adapter 將 `calls` 轉為 `tool_calls`，將 parsed
`arguments` 編碼成 JSON string、raw form 原樣傳回；tool result 則轉為帶 `tool_call_id` 的 tool message。canonical messages
本身不被某個 provider 的偶然欄位綁住。

## system 與 clear

`messages set system TEXT` 會插入或取代 local system；`unset system` 移除 local override，讓 source system
重新露出。它們可在 idle／paused 且沒有 pending call 時修改；waiting 必定欠 ask_user result，所以拒絕。
`messages clear` 清除 conversation、保留 system，
對應現有 Bot reset 的直覺；`messages clear --all` 才全部清空。兩種 clear 只在沒有 active Run 時允許，
避免把目前工作的 instruction 與 history 拆開。
