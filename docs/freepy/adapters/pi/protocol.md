# Pi bridge protocol 與控制語意

[← 返回 Pi adapter 總覽](README.md)

## 資料與控制流

一般觀察流程：

```text
人 → Pi prompt
  → Pi 呼叫 agentloop_control(status/wait/...)
  → extension 寫一行 JSON command 給 bridge
  → bridge main thread 操作或讀取 Handle
  → bridge 寫一行帶 request id 的 JSON response
  → extension 回傳精簡 tool result 給 Pi
  → Pi 解釋狀態或決定下一個 action
```

agentloop 邊界事件：

```text
runner: ask() → commit Step → after_step callback
                                   │ snapshot/event
                                   ▼
                              bridge event queue
runner: tools → commit results → after_tools callback
                                      │ snapshot/event
                                      ▼
                                 bridge event queue
```

Bridge protocol 應使用 request id 對應 response，event 則有單調遞增 sequence number、Round
id、boundary type、`state` 與 `step`。stdout 只放 protocol JSONL，診斷一律寫 stderr，避免
破壞 framing。大型 message/tool result 必須截斷並提供「已截斷」metadata；完整資料可由明確
的 detail action 分頁取得。

建議 snapshot 僅包含可序列化報告：`state`、`step`、`message`、`tool_calls`、
`tool_results`、`tool_log` 摘要、token counters、`stop`、`end_reason`、`err` 的安全文字形式。

## pause、resume、end 與 callbacks 的映射

| Pi action | Python 行為 | 邊界語意 |
|---|---|---|
| `pause` | `handle.pause()` | `ready`／parked 時立即 paused；running 時完成 Step+callbacks 或 tool batch+callbacks 才停 |
| `resume` | 必要修改包在 `handle.edit()`，再 `handle.resume()` | 只喚醒 `waiting`／`paused` 或取消尚未生效的 pause |
| `end` | `handle.end(reason="pi")` | 已在安全邊界時立即完成；running 時等下一個邊界，不強殺 operation |
| `status` | 在 lock 下製作 JSON snapshot | 只觀察，不改控制流 |
| `edit` | `with handle.edit(): ...` | 修改本身不會自動 resume，也不代表 Round 未完成 |

Callbacks 仍在 Python runner thread、持有 Handle 的 `RLock` 同步執行。Pi extension 不可能
成為真正的同步 callback：它在另一個 process，事件送達時 runner 可能早已進入下一個
operation。因此 callback 只能做兩類事：

- **observe mode**：callback 把 snapshot 放進 Python queue，回 `CONTINUE`；Pi 看到的是
  事後通知，不能保證趕在 tools 或下一個 Step 前修改。
- **gate mode**：callback 先把 snapshot 放入 queue，再回 `PAUSE`；runner 安全停住後，Pi
  才能可靠檢查／修改並 `resume()` 或 `end()`。

推薦最小版支援可配置的 gate：`after_step`、`after_tools` 各自可設 `continue` 或 `pause`。
若要批准或改寫 tool calls，必須讓 `after_step` gate 回 `PAUSE`；若要改寫 tool results，
必須讓 `after_tools` gate 回 `PAUSE`。不要讓 callback 等待 Pi response，也不要在 callback
裡啟動 child thread 後 `join()`；callback 應只 enqueue immutable snapshot 並迅速返回。

Agentloop callback 自己回 `END` 與 Pi 呼叫 `end` 最終都進入 Handle 的完成狀態，但發起者
不同。Bridge 應保留 `stop`／`end_reason`，讓 Pi 不要把 Limits、callback policy 與人工結束
混為一談。

## 已實作的 scripts 與 example

```text
freepy/adapters/pi/
├── pi-agentloop.ts           # Pi extension、commands、child lifecycle、JSONL client
├── pi_bridge.py              # Python bridge、Handle、runner、callback events
├── check_pi_bridge.py        # 離線 subprocess protocol check
└── examples/minimal_factory.py

docs/freepy/adapters/pi/quickstart.md
```

Bridge 預設 `after_step=pause`、`after_tools=continue`；`start` 可透過 `gates`
覆寫。Factory 只能由 bridge 啟動參數或 `AGENTLOOP_PI_FACTORY` 指定，不接受
Pi tool payload 動態指定 import path。完整用法見 [Pi quickstart](quickstart.md)。
