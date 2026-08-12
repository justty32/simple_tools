# 以 Claude Code 操作 agentloop

本文記錄把 Claude Code 當作 agentloop 進階互動入口的初步設計。這一階段只比較官方提供的
擴充方式、定義 process／thread 邊界並提出第一版架構，不實作 adapter。

Claude Code API 與文件最後核對日期為 **2026-08-11**。本文只使用 Anthropic 官方文件與
官方 repository；正式實作時仍應鎖定 Claude Code／Agent SDK 版本並重新核對 schema。

## 文件導覽

- [MCP 介面](mcp-interface.md)：主入口選擇、tool contract、snapshot 與 event。
- [Handle 與控制邊界](control-boundaries.md)：process/thread ownership、callbacks 與 pause／resume／end。
- [Claude Code 整合](host-integration.md)：skills、hooks 與 teardown 的正確角色。
- [方案與驗收](validation.md)：可行方案比較與第一個端到端場景。
- [風險、待決問題與來源](risks-and-sources.md)：限制、未定事項及官方資料。

## 結論

第一版推薦：

```text
Claude Code process（現成互動式 TUI）
  ├─ Claude Code 自己的 agent loop 與 coding tools
  ├─ project skill：教 Claude 何時及如何操作 agentloop
  └─ MCP client
       │ stdio MCP
       ▼
Python agentloop MCP server process
  ├─ MCP/main thread：處理 start/status/wait/edit/pause/resume/end
  └─ runner thread：agentloop.threading.start(...)
       └─ Handle、bot、dispatch、callbacks 全留在這個 Python process
```

Claude Code 已原生支援本機 stdio MCP server，所以不需要仿照 Pi 再寫一層 TypeScript
extension 與自訂 JSONL protocol。MCP 本身就是兩個 process 之間的正式工具介面；Python
server 同時是 agentloop controller，真正的 `Handle` 不離開 Python process。

先用 project-local `.mcp.json` 加一個 `.claude/skills/agentloop/SKILL.md` 驗證設計。等工具
名稱、factory 介面和 lifecycle 穩定後，再包成 Claude Code plugin，讓 plugin 一次攜帶
skill、MCP server 設定與可選 hooks。

## 整合目標

- 保留 Claude Code 現成的 terminal UI、session、permissions 與 coding tools。
- 人可以用自然語言或 skill command，讓 Claude 觀察、暫停、修改、恢復或結束一個 Round。
- Claude 與人最後都走同一組 MCP tools，不替任何一方建立 agentloop 隱藏控制入口。
- agentloop 核心不認識 Claude Code；adapter 只組合公開的 `Handle`、callbacks 與
  `agentloop.threading.start()`。
- 可靠保留 `after_step`（tool calls 已提交、tools 尚未執行）及 `after_tools`（results 已
  提交、下一 Step 尚未開始）兩個介入點。
- 第一版只管理一個 active Round，用真實任務驗證控制 API，不先建立多-agent runtime。

## 非目標

- 不把 Claude Code session 與 agentloop Round 當成同一個 lifecycle。
- 不讓 `Handle`、lock、bot、Python callable 或 exception 跨 process 傳遞。
- 不用 Claude Code 的 interrupt、hook 或 permission system 模擬 agentloop 的 `pause()`。
- 不同步兩邊的完整 conversation history，也不混合兩邊各自的 tool calls。
- 不提供 sandbox、持久化 Round、多 Handle registry、scheduler 或 crash recovery。
- 不允許 MCP input 傳任意 pickle、任意 Python expression 或任意 import path 後直接執行。
