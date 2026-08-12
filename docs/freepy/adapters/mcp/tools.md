# MCP 控制工具契約

[← 返回 MCP adapter 總覽](README.md)

## 第一版 tools

應使用分開的 tools，而不是一個接受任意 `action` 的萬用 tool。分開後模型較容易選對工具，
host 也較可能針對 `edit`／`end` 等高風險能力做獨立批准。所有結果都回傳
`structuredContent`，另附短文字摘要供只展示文字的 client 使用。

| MCP tool | agentloop 行為 | 建議結果 |
|---|---|---|
| `agentloop_start(profile, prompt, ...)` | 由啟動時註冊的具名 factory 建 bot／dispatch／Handle，再 `threading.start()` | `round_id`、初始 state、revision |
| `agentloop_status(round_id, detail)` | 在 Handle lock 下複製 snapshot | state、step、calls、token、stop、event sequence、按 detail 截斷的資料 |
| `agentloop_wait(round_id, after_seq, states, timeout_ms)` | 等待下一個 boundary/state event；不持有 Handle lock 長期等待 | events、最新 snapshot、`timed_out` |
| `agentloop_pause(round_id)` | `handle.pause()` | accepted、當下 state；ready／parked 可立即 paused |
| `agentloop_resume(round_id)` | `handle.resume()` | accepted、當下 state |
| `agentloop_end(round_id, reason)` | `handle.end(reason=...)` | accepted、當下 state；running 時尚未表示已完成 |
| `agentloop_edit(round_id, expected_revision, operations)` | `with handle.edit():` 內驗證並原子套用 | 新 revision 與修改後摘要 |

`start` 不接受 pickle、任意 Python expression、import path 或任意 callable。可用哪些 bot、
dispatch 與預設 Limits，應由啟動 server 時設定的具名 profile/factory 決定。第一版可以只允許
一個 active Round，但 wire format 仍保留 `round_id`。

### status

Snapshot 建議包括：

```text
round_id, revision, state, step, message, text, finish_reason,
tool_calls, tool_results, tool_log 摘要,
calls, used, input_tokens, output_tokens, cached_input_tokens,
auto_finish, stop, end_reason, err 的安全文字摘要,
latest_event_seq
```

預設結果必須有長度上限。完整 message、tool result 與 log 以 `detail`、分頁或日後 Resource
另外取得；不可把 exception object、bot、history object、dispatch callables、lock 或任意
不可序列化資料搬出 process。

### wait 與 cancellation

`agentloop_wait` 是 bounded long-poll：client 傳入上次收到的 `after_seq`，server 返回較新的
事件，或在 timeout 後返回 `timed_out=true`。Server 應限制最大 timeout，避免 coding agent
的一次 tool call 永久卡住。client 斷線或取消 `wait`，只取消這次等待，**不等於**
`pause()`、`end()` 或中斷 agentloop 正在執行的模型／工具。

事件 queue 應有固定上限；若 client 落後到事件已被淘汰，回傳 `gap=true` 並要求重新讀
`status`。`seq` 只在單一 Round 內單調遞增，不拿 wall-clock timestamp 當順序。

### pause、resume 與 end

MCP response 必須保留 agentloop 的合作式語意：

- `pause` 在 `ready`／parked 時可立即成為 `paused`；在 `running_step`／`running_tools` 時只表示
  要求已接受，真正停住要再 `wait` 到
  `paused`，或讀取 `status`。
- `resume` 只喚醒 `waiting`／`paused`，或取消尚未生效的 pause；修改資料本身不自動 resume。
- `end` 在 `ready`／parked 時可立即完成；running 時要等現有 Step+callbacks 或 tool batch+callbacks
  的安全邊界。`accepted=true` 不等於 `state=completed`。
- `safe=False` 不應出現在 MCP schema；adapter 不提供核心本來就不支援的 unsafe 操作。

### edit

Python 同 process API 刻意允許操作者直接修改 Handle；跨 process MCP 不可能保留「任意
Python object」的自由度。第一版只接受明確的 JSON operations 與欄位 allowlist，例如：

```text
prompt, images, ask_options, tool_calls, tool_results, auto_finish
```

不要接受任意 attribute path，尤其不可改 `_lock`、`_condition`、`_runner`、內部 request
flags、bot 或 dispatch callable。若要變更 dispatch，只能切換 server 事先註冊的 profile／
tool binding，不能從 MCP payload 注入 code。

`expected_revision` 用來避免兩個 controllers 在不知情下互相覆蓋；不符合時回 conflict 與
最新 revision。若呼叫者明確接受 last-writer-wins，可省略它。單次 operations 必須先完整
驗證，再在一次 `handle.edit()` 中原子套用；失敗不可留下半套修改。
