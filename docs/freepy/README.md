# FreePy 文件地圖

這裡只放跨 package 架構、interfaces/adapters 與尚未實作的規格。實際 API 與使用方式留在
[`freepy/`](../../freepy/README.md) 的 package 旁。

## 目前定案

- [`architecture.md`](architecture.md)：core → Controller → interfaces/libraries → outer services。
- [`agent-machine/`](agent-machine/README.md)：目前主線；Python 參考實作到 C++23／Janet 的施工計畫。
- [`interfaces/python-repl.md`](interfaces/python-repl.md)：直接 Python REPL 的操作方式。
- [`adapters/`](adapters/README.md)：Pi、MCP、Claude Code、Codex 的 adapter 文件。
- [`ollama-192.168.1.146.md`](ollama-192.168.1.146.md)：本地 Ollama 與 FreePy 的實測證據。

## 未來規格與研究底稿

[`future/`](future/README.md) 保存尚未完成或刻意延後的設計，不是現況 API。舊 Agent Machine
模型也留在其中作研究來源；實作順序以新的 active plan 與 Roadmap 為準。

白話 HTML 從 [`../guides/roadmap/`](../guides/roadmap/index.html) 開始；更廣的設計底圖在
[`../design/agent-world/`](../design/agent-world/README.md)。
