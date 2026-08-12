# FreePy 文件地圖

這裡只放跨 package 架構、interfaces/adapters 與尚未實作的規格。實際 API 與使用方式留在
[`freepy/`](../../freepy/README.md) 的 package 旁。

## 目前定案

- [`architecture.md`](architecture.md)：core → Controller → interfaces/libraries → outer services。
- [`interfaces/python-repl.md`](interfaces/python-repl.md)：直接 Python REPL 的操作方式。
- [`adapters/`](adapters/README.md)：Pi、MCP、Claude Code、Codex 的 adapter 文件。

## 未來規格

[`future/`](future/README.md) 全部都是尚未完成或刻意延後的設計，不是現況 API。包含 Agent
Machine、control plane、runtime、memory、team、communication、exec discovery、introspection 與
agentfs。

白話 HTML 從 [`../guides/roadmap/`](../guides/roadmap/index.html) 開始；更廣的設計底圖在
[`../design/agent-world/`](../design/agent-world/README.md)。
