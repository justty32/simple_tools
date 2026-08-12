# 以 MCP 操作 agentloop

本文研究把 Model Context Protocol（MCP）作為 agentloop 的通用跨 process 互動入口。
這一階段只定義設計，不實作 server。資料僅採用 MCP 官方 specification、官方文件與官方
Python SDK；最後核對日期為 **2026-08-11**。

## 結論

第一版應做一個 **Python MCP server，以 stdio 啟動，server process 內持有真正的
`Handle` 與 runner thread**。主要能力全部做成 MCP tools：

- `agentloop_start`
- `agentloop_status`
- `agentloop_wait`
- `agentloop_pause`
- `agentloop_resume`
- `agentloop_end`
- `agentloop_edit`

每次呼叫都明確傳入 opaque `round_id`。`status` 與 `wait` 是可靠的基線；MCP Resource 與
resource update notification 只作日後的便利層，不作正確性依賴。Prompts、sampling、
elicitation 都不是第一版必要能力。

推薦拓撲：

```text
Pi／Claude Code／Codex／其他 MCP host
        │ MCP（第一版 stdio）
        ▼
Python agentloop MCP server process
  ├─ MCP request handlers：controller
  ├─ Round registry：round_id → Handle／BackgroundRun／event queue
  └─ runner thread：agentloop.run(...)
```

這和 Pi 文件提出的 JSONL bridge 是同一種 process 邊界，但 MCP 已經提供 tool discovery、
schema、request correlation 與多 host 接入。若各 coding agent 的 MCP client 足以使用這些
tools，應優先共用 MCP server；產品專用 extension 只補 UI、slash command、權限確認與
lifecycle，不再各寫一套 Python bridge protocol。

## 文件導覽

- [Transport 與協議生命週期](transport.md)：為何先採 stdio，以及 MCP revision 對 Round identity 的影響。
- [控制工具契約](tools.md)：第一版 tools、snapshot、等待、控制桿與 edit 語意。
- [事件與 MCP capabilities](events-and-capabilities.md)：callback gate、resources、prompts、notifications、sampling 與 elicitation。
- [Host、安全與 server lifecycle](lifecycle-and-security.md)：client 能力差異、授權、Round 清理、入口分工、實作順序與來源。
