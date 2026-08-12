# MCP transport 與協議生命週期

[← 返回 MCP adapter 總覽](README.md)

## 為何先用 stdio

MCP 官方 Python SDK 把 `stdio` 定位為由 host 啟動的本機 subprocess transport，把
Streamable HTTP 定位為需要部署的網路 server。第一版的目標正是讓本機 coding agent 控制
同一台機器上的 Python Round，因此 stdio 最合適：

- 不必開 port、做 service discovery 或先建 OAuth；
- MCP server 和 coding-agent session 可以採相同 process lifecycle；
- `Handle`、bot、dispatch callables 與 lock 全留在 Python process；
- stdout 由 MCP protocol 獨占，診斷只寫 stderr。

stdio 並不提供 sandbox 或較低權限。server 與 agentloop tools 仍擁有啟動它的使用者權限。
host 關閉或強殺 child process 時，正在執行的模型請求／工具也會一起被作業系統終止，這和
`Handle.end()` 的合作式安全結束不同。

Streamable HTTP 適合第二階段：獨立 service、重連、多個 hosts 或遠端控制。它同時會立刻
帶來 authentication、authorization、Origin／DNS rebinding、防止跨使用者猜中 `round_id`、
多 client 競爭及部署維護等責任，因此不應只是「把 stdio 改成一個 port」。

舊 HTTP+SSE transport 已被 Streamable HTTP 取代且正式 deprecated，不應做新入口。

## 最新 MCP lifecycle 對本設計的影響

目前官方最新 protocol revision 是 `2026-07-28`。此版移除了 `initialize`／`initialized`
handshake、`Mcp-Session-Id` 與 HTTP GET session stream。每個 request 都攜帶 protocol
version、client capabilities 等 `_meta`，server capability 可由 `server/discover` 查詢。

這表示：

- **MCP connection/session 不是 agentloop Round。** Server 必須由 `agentloop_start` 產生
  明確的 `round_id`，後續每個 tool call 都帶回它。
- `round_id` 是應用層 reference，不是 Python `Handle` 的序列化形式，也不是憑證。
- Streamable HTTP 的每次 request 可能落在不同 worker；若未來多 worker，Round registry
  必須集中或由 round id 做明確 routing。第一版不要多 worker。
- stdio 的單一長壽 process 雖然自然保留記憶體，仍不應省略 `round_id`，否則未來無法平順
  遷移到 HTTP，也容易把 host session、server process 與 Round lifecycle 混成一件事。

官方 Python SDK v2 是目前 stable line，支援 `2026-07-28` 及較早 revisions。實作時應 pin
SDK 的相容版本並使用它的 protocol negotiation／compatibility，不自行寫 JSON-RPC parser。
較舊 clients 仍可能使用 initialize、transport session 與舊式 notification；adapter 自己的
Round API 仍維持明確 `round_id`，不隨協議版本改變。
