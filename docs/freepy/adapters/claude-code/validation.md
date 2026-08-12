# Claude Code adapter 方案與驗收

[← 返回 Claude Code adapter 總覽](README.md)

## 可行方案比較

| 方案 | 優點 | 問題 | 結論 |
|---|---|---|---|
| **互動式 Claude Code + local stdio MCP + skill** | 保留完整 TUI；Python server 直接持有 Handle；使用官方 custom-tool 通道；最少自訂層 | 要管理 child/runner lifecycle；遠端只看可序列化狀態；stdio crash 不 reconnect | **推薦第一版** |
| 只做 skill／command，透過 Bash 操作 Python REPL | 幾乎零 adapter | 每次 shell 是獨立 process；REPL/tmux 解析脆弱；無可靠 Handle ownership 或 callback gate | 不採用 |
| Claude Code hooks + scripts | 有 deterministic lifecycle trigger，也能限制 Claude tools | hooks 控制的是 Claude Code，不是 agentloop；command hook process 無法取得既有 Handle | 只作 policy/cleanup 配角 |
| 打包成 Claude Code plugin | 可一起配送 skill、MCP config、hooks；啟用時自動啟動 MCP server | 太早打包會把未穩定 protocol 固化；仍是同一個 MCP 架構 | 第二階段 |
| Python Agent SDK + in-process SDK MCP tools | Python app、Handle 與 custom tool callbacks 同 process；`ClaudeSDKClient` 支援多 turn、hooks、interrupt | Python app 變成 top-level UI；失去原生 interactive TUI；interrupt 仍不是 Handle pause | 自訂產品／automation 很合適，不是第一個人類入口 |
| `claude -p` + stream-json subprocess | 官方 CLI 支援 stream-json input/output、turn/budget limits | 要自己做 host UI、stream lifecycle 與錯誤處理；比 Agent SDK 低階 | CI／一次性自動化 |
| 獨立 HTTP MCP service | 可跨 Claude session、支援多 client 與 reconnect | 立即需要 auth、registry、租約、衝突與 persistence policy | 有長壽 Round 需求後再做 |
| Claude Code Channels 推送 boundary event | 可主動把外部事件送入活著的 session | research preview、平台限制；事件會驅動 Claude turn，增加循環與成本風險 | 暫不依賴 |

## 第一個端到端範例應驗證什麼

後續第一個實作／example 應刻意小：

1. Claude Code 透過 MCP `start` 啟動 `auto_finish=False` Round。
2. `after_step` gate 看見一個 tool call，enqueue snapshot 並回 `PAUSE`。
3. Claude `wait/status` 取得同一 step/sequence，說明風險。
4. 人允許後，Claude 用具 expected version 的 `edit` 改寫 call，再 `resume`。
5. `after_tools` observe 記錄結果；Round 下一 Step 自然進入 `waiting`。
6. Claude status 後由人決定追加 prompt/resume，或 `end`。
7. server join，回報 completed、stop/end_reason 及 token counters。

這一條流程會同時驗證 Handle 不跨 process、MCP schema、stale-write protection、callback gate、
安全 pause/resume/end 與正常 teardown。
