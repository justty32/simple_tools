# Claude Code 與 agentloop 的控制邊界

[← 返回 Claude Code adapter 總覽](README.md)

## Handle、thread 與 process 邊界

```text
Claude Code                         Python MCP server
-----------                         -----------------
呼叫 MCP tool          --------->   MCP handler/controller
                                    真正的 Handle
                                    runner = start(...)
                                        │
                                        └─ 唯一 runner thread
status/edit/pause/...   --------->   controller 與 runner 共享 Handle
JSON snapshot/result    <---------   Python object 留在 server process
```

一個 Handle 仍是 one-shot，只能對應一個 Round 及一條 runner thread。Claude Code session 的
`/resume`、branch 或 SDK fork 只複製／恢復 Claude 的 conversation，不會 fork Python
Handle，也不會讓已死的 stdio server 恢復記憶體狀態。

MCP handler 與 runner thread 做複合修改時使用 `with handle.edit()`。若 MCP framework 使用
async event loop，阻塞的 `wait_for_state()`／`runner.join()` 應移到 worker thread 或採短 timeout
輪詢，不能阻塞整個 protocol loop。

## agentloop callbacks：observe 與 gate

Claude Code 在另一個 process，不能成為 agentloop 的同步 callback。adapter callback 只能做：

- **observe mode**：建立 immutable snapshot、放入有上限的 Python event queue，回
  `CONTINUE`。Claude 稍後才看到通知，runner 可能已進入下一 operation。
- **gate mode**：enqueue snapshot 後回 `PAUSE`。runner 在 callback 結束後安全停住，Claude
  再用 MCP `status`／`wait` 查看，透過 `edit` 修改，最後 `resume` 或 `end`。

如果目標是批准或改寫 agentloop 的 tool calls，必須讓 `after_step` gate 回 `PAUSE`；如果要
改寫 tool results，必須讓 `after_tools` gate 回 `PAUSE`。第一個端到端範例建議預設：

```text
after_step = gate（展示 tools 執行前審核）
after_tools = observe（避免每批結果都強迫停住）
```

callback 在 runner thread 且持有 Handle 的 `RLock` 執行，因此它只應複製 snapshot、enqueue
並迅速返回。不可在 callback 裡等待 Claude/MCP response，也不可建立需要存取同一 Handle
的 child thread 後當場 `join()`。

一般 MCP server 不會無條件把 boundary event 主動變成 Claude 的新 turn；第一版使用有
timeout 的 `wait` tool 或由 skill 指示 Claude 在必要時查詢。Claude Code Channels 雖能讓 MCP
server push event 進活著的 session，但目前是 research preview、有驗證與部署限制，而且會
讓 observe event 自動驅動另一個 agent turn；不應成為第一版依賴。

## pause、resume、end 的映射

| Claude/MCP action | Python 行為 | agentloop 語意 |
|---|---|---|
| `pause` | `handle.pause()` | ready／parked 時立即 paused；running 時完成 Step+callbacks 或 tool batch+callbacks 才停 |
| `resume` | `with handle.edit(): ...` 後 `handle.resume()` | 只喚醒 waiting/paused，或取消尚未生效的 pause |
| `end` | `handle.end(reason="claude-code")` | ready／parked 時立即完成；running 時等下一安全邊界，不強殺 operation |
| `status` | lock 下產生 snapshot | 只觀察，不改控制流 |
| `edit` | `with handle.edit(): ...` | 修改不會自行喚醒，也不代表 Round 未完成 |

Claude Code 的 Ctrl-C、Agent SDK `interrupt()` 與 agentloop `pause()`／`end()` 控制的是不同
agent loop。前兩者中止 Claude Code 當前 turn，不會自動安全暫停 Python runner；反過來，
agentloop parked 也不代表 Claude Code turn 被中止。
