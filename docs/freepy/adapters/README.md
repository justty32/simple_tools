# Control adapter 文件

| 文件 | 狀態 |
|---|---|
| [Pi coding agent](pi/README.md) | 已實作部分、bridge 設計與風險 |
| [Pi quickstart](pi/quickstart.md) | 已實作 adapter 的操作方式 |
| [MCP](mcp/README.md) | 通用 server 設計，尚未實作 |
| [Claude Code](claude-code/README.md) | host-specific 研究，尚未實作 |
| [Codex](codex/README.md) | host-specific 研究，尚未實作 |

每個 adapter 目錄的 `README.md` 是入口；較長的架構、控制邊界、風險與來源依角色拆在
相鄰子頁，避免單一文件同時承擔導覽與完整研究正文。

可執行 Pi 程式在 [`freepy/adapters/pi/`](../../../freepy/adapters/pi/README.md)。這些 adapters 操作
獨立 agentloop，不借 host endpoint，也不接管 host 內部 loop。
