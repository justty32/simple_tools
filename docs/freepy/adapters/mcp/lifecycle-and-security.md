# MCP host、安全與 server lifecycle

[← 返回 MCP adapter 總覽](README.md)

## Client capability 差異

「支援 MCP」不代表 host 會完整提供 tools、resources、prompts、subscriptions、elicitation
或一致的人工批准 UI。Adapter 應：

1. 把 **tools/call + polling wait** 當最低共同能力；
2. 依 request `_meta` 的 client capabilities 決定是否提供 MRTR elicitation 等增強流程；
3. 沒有 resource subscription 時仍能用 sequence + status 恢復；
4. 不假設 prompt 一定顯示成 slash command、resource 一定自動放進模型 context；
5. 對不支援必要 tool calling 的 host 明確報錯，而不是另開隱藏控制通道。

正式實作前，仍需由 Pi、Claude Code、Codex 各自的 adapter 調查文件確認：它們支援哪些 MCP
transport／protocol revision、是否向模型暴露 structured results、是否能批准 mutating tools、
是否支援 resources/subscriptions/elicitation。MCP server 的核心 tool schema 不因 host 改變，
host-specific layer 只補呈現與 lifecycle。

## 安全與權限

- `round_id` 只用來定位 Round，不是 authorization token。Streamable HTTP 必須把每次 request
  的認證 principal 與 Round owner／ACL 綁定。
- `status` 也可能洩漏 prompt、路徑、原始 tool arguments/results 與 secrets；預設要截斷、
  redact，詳細資料需較高權限。
- `edit`、`resume`、`end` 與 `start` 是 mutating operations，應能和 read-only
  `status`／`wait` 分開授權與批准。
- tool annotations 只能是 UI hint；官方規格要求 client 把來自不可信 server 的 annotations
  視為不可信，server 端不可用它代替 enforcement。
- 不允許任意 code/import/pickle、私有欄位、callable 或 exception object 穿過 JSON 邊界。
- 多 controller 修改要使用 revision conflict；同一 Handle 仍只有一條 runner thread。
- event queue、result size、wait timeout、active Round 數量與 completed retention 都要有上限，
  防止記憶體與 thread 耗盡。
- 若使用 Streamable HTTP，應採官方 authorization guidance，驗證 Origin／Host 並防 DNS
  rebinding；不要因只 bind localhost 就假設瀏覽器無法攻擊。
- 這個 MCP server 不是 sandbox。Agentloop dispatch 的檔案、shell、網路等權限仍需由外部
  process/container/policy 決定。

## Round 與 server lifecycle

第一版建議每個 registry entry 保存：

```text
round_id, Handle, BackgroundRun, event queue, revision,
created_at, last_accessed_at, owner（HTTP 才需要）
```

- `completed`／`error` 後保留有限時間供 status 讀取，再明確 evict。
- MCP server 正常 shutdown 時，對 active Rounds 呼叫 `end(reason="server_shutdown")` 並嘗試
  `join`。因為 cooperative end 無法切斷已開始的模型／工具，shutdown 可能無法在固定時間內
  完成；部署層最後強殺 process 時要承認這是 unsafe termination。
- stdio EOF 代表 host 已離開，不代表 Round 自然完成。第一版採同 lifecycle：best-effort
  end，再退出；不承諾 reconnect。
- Streamable HTTP server restart 會失去 process-local Handle。要支援 reconnect／持久化，需在
  agentloop 上層新增可恢復 runtime，不能只保存 JSON snapshot 後假裝 Python runner 還活著。
- 一個 Pi／Claude Code／Codex session 可控制多個 Round；反過來，host 的 fork/resume/new
  不會 fork、轉移或重用 Handle。

## 與各種入口的關係

```text
Python REPL
  └─ 同 process 直接持有 Handle：自由度最高

通用 MCP server
  └─ 跨 process、JSON allowlist、可被多種 MCP host 共用

Pi／Claude Code／Codex 專用 adapter
  └─ 呼叫相同 MCP tools，補各產品的 UI、command、approval、啟停與設定
```

Python REPL 繼續是「全部公開狀態、操作者承擔風險」的基準入口。MCP 是遠端 controller API，
必須承認 serialization、authorization 與 concurrency 邊界，因此不應聲稱完整等價於直接
持有 Handle。

對 Pi 而言，若其 MCP client 能滿足最小工具需求，先前規劃的 TypeScript extension 可以縮成
UX layer，或完全由 MCP 設定取代；若需要 Pi 特有 slash commands／TUI gate，extension 仍可
呼叫 MCP client，而 Python 端只保留一份 server。Claude Code 與 Codex 也遵循同一原則：先用
共同 MCP tools，只有官方 MCP host 缺少必要 lifecycle/UI 時才寫薄的產品 adapter。

## 建議實作順序

1. 用官方 Python SDK v2 建 stdio server，單 process、單 active Round、明確 `round_id`。
2. 實作 start/status/wait/pause/resume/end；snapshot 截斷、event seq 與 bounded queue。
3. 加 after-step／after-tools observe/gate callbacks，驗證安全邊界。
4. 實作 allowlisted atomic edit 與 optional `expected_revision`。
5. 用 MCP Inspector 加至少一個真實 coding-agent host 做端到端測試。
6. 根據各 host 實測，再決定 resources/subscriptions、elicitation 或專用 UX adapter。
7. 只有出現跨 session／多 client／遠端需求後，才做 Streamable HTTP、認證與 Round service。

## 官方來源

以下來源最後核對於 2026-08-11：

- [MCP 2026-07-28 specification](https://modelcontextprotocol.io/specification/2026-07-28)
- [2026-07-28 版本變更說明](https://blog.modelcontextprotocol.io/posts/2026-07-28/)
- [Streamable HTTP transport](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http)
- [stdio transport](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio)
- [Tools](https://modelcontextprotocol.io/specification/2026-07-28/server/tools)
- [Resources 與 subscriptions](https://modelcontextprotocol.io/specification/2026-07-28/server/resources)
- [Prompts](https://modelcontextprotocol.io/specification/2026-07-28/server/prompts)
- [Elicitation](https://modelcontextprotocol.io/specification/2026-07-28/client/elicitation)
- [Sampling（已 deprecated）](https://modelcontextprotocol.io/specification/2026-07-28/client/sampling)
- [官方 Python SDK](https://github.com/modelcontextprotocol/python-sdk)
- [官方 Python SDK：transport 選擇](https://py.sdk.modelcontextprotocol.io/run/)
