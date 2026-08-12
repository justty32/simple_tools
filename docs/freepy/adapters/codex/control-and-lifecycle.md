# Codex 與 agentloop 的控制及生命週期

[← 返回 Codex adapter 總覽](README.md)

## Callback：observe 與 gate

Codex 在另一個 process，不能成為 agentloop 的同步 callback。若 callback 回 `CONTINUE`，
Codex 看到通知時 runner 可能已開始下一個 operation。

因此保留兩種模式：

- **observe**：callback enqueue snapshot 後回 `CONTINUE`。適合記錄與事後觀察。
- **gate**：callback enqueue snapshot 後回 `PAUSE`。runner 停在安全邊界，Codex 再檢查、
  修改並 `resume()` 或 `end()`。

如果要批准或改寫模型剛產生的 tool calls，`after_step` 必須採 gate；如果要改寫 tools 的
results，`after_tools` 必須採 gate。Callback 本身只 enqueue 並快速返回，不等待 Codex 回應，
也不建立會操作同一 Handle 後又被 callback `join()` 的 thread。

第一個端到端範例建議預設：

```text
after_step = pause
after_tools = pause
auto_finish = false
```

這雖然較慢，卻能一次驗證兩個介入點。證實可靠後，常態可改為
`after_step=pause, after_tools=continue`，或完全 observe。

## pause、resume、end 的精確映射

| MCP tool | Python 行為 | 真正語意 |
|---|---|---|
| `agentloop_pause` | `handle.pause()` | ready／parked 時立即 paused；running 時完成 Step+callbacks 或整批 tools+callbacks 才停 |
| `agentloop_edit` | `with handle.edit(): ...` | 原子修改資料；不會自行喚醒 runner |
| `agentloop_resume` | `handle.resume()` | 喚醒 waiting／paused，或取消尚未生效的 pause |
| `agentloop_end` | `handle.end(reason="codex")` | ready／parked 時立即完成；running 時等下一個安全邊界 |
| `agentloop_wait` | 等 Handle state／event queue | 只同步觀察，不改控制流 |

MCP response 要分開表示 `accepted` 與目前 `state`。若回應時仍在 running，`pause()` 的 `True`
只表示要求已接受；需要可靠修改 tool calls 時，Codex 必須再 `wait` 到 `paused`。

agentloop callback 回 `END`、Limits callback 回 `END` 與 Codex 呼叫 `agentloop_end` 最後都會
結束 Round，但起因不同。Snapshot 應保留 `stop` 與 `end_reason`，不要全部顯示成「Codex
停止」。

## 不要混淆 Codex 的控制流

Codex app-server 本身有 thread、turn、item，也有 `turn/steer` 和 `turn/interrupt`：

- `turn/steer` 是把輸入追加到正在進行的 **Codex turn**；
- `turn/interrupt` 是要求取消 **Codex turn**，完成狀態會是 `interrupted`；
- `agentloop_pause/resume/end` 控制的是 Python process 裡的 **agentloop Round**。

中斷 Codex turn 不會自動暫停 agentloop；暫停 agentloop 也不會中斷 Codex。若人中斷 Codex
時 agentloop 仍在 running／waiting，MCP server 仍必須依自己的 lifecycle policy 管理它。
第一版不要暗中把兩者綁在一起。

## Process 與生命週期

Codex 只持有 MCP connection；Python MCP process 持有真正 Handle。Server 內部可用一把
registry lock 保護 active Round reference，Handle 自己的複合修改仍使用 `handle.edit()`。
即使 Codex 通常循序呼叫 tools，也不要把「永遠只有一個同時 request」當作正確性前提。

第一版採一個 MCP process、一個 active Handle、一條 agentloop runner thread：

1. MCP initialize 後 server 尚未建立 Round。
2. `agentloop_start` 建立 `Handle(auto_finish=False)`，註冊 callbacks，再啟動 runner。
3. Codex 以 status/wait/control tools 操作它。
4. 正常結束時 `end()`（若需要）後 `runner.join()`，再清除 registry。
5. MCP transport 收到 EOF、shutdown 或 process signal 時，對 active Round 呼叫 `end()`，
   有限時等待 `join()`，並把診斷寫到 stderr。

agentloop 沒有 unsafe cancellation。若 model request 或 Python tool 永久卡住，`end()` 也只能
等待安全邊界。為避免 STDIO parent 關閉後留下不能退出的 child，bridge runner 可明確使用
`daemon=True`，但文件與 log 必須承認：shutdown timeout 後 Python process 結束會切斷 operation，
不保證 callback 或 history 收尾。正常路徑仍一律 `end()` 加 `join()`。

官方文件確認 STDIO MCP 是由 command 啟動的 local process，但沒有承諾它是跨 Codex client
重啟的 durable Round host。因此第一版把 MCP process 消失視為 Handle 消失；不要只把
`round_id` 寫進 Codex thread 後假裝可重連。

若未來需要跨 Codex session 保留 Round、多個 Codex clients 共用或 server reload 後重連，
再把同一套 MCP tools 搬到獨立的 Streamable HTTP service，加入 authentication、Round registry、
lease／controller ownership、持久化與 conflict policy。這是 runtime 升級，不是 agentloop
核心修改。
