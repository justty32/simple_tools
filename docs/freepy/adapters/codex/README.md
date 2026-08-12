# 以 OpenAI Codex 操作 agentloop

本文記錄把 OpenAI Codex 當作 agentloop 進階互動入口的初步設計。這一階段只比較官方
提供的擴充與程式化介面、定義 process 邊界並提出第一版架構，不實作 adapter。

官方 Codex manual 與各介面最後核對日期為 **2026-08-11**。

## 文件導覽

- [MCP 介面](mcp-interface.md)：公開工具、snapshot 與 event queue。
- [控制與生命週期](control-and-lifecycle.md)：callback gate、控制桿、兩套控制流與 process teardown。
- [Codex host 整合](host-integration.md)：方案選擇、plugin／skill 分工與權限。
- [交付、待決問題與來源](delivery.md)：預計 artifacts、尚待決策事項及官方文件。

## 結論

第一版推薦：

```text
Codex CLI／TUI
  ├─ agentloop skill：教 Codex 何時觀察、暫停、修改、恢復或結束
  └─ project-scoped STDIO MCP server
       │ MCP tools
       ▼
Python MCP process
  ├─ MCP/controller thread：處理命令、產生 snapshot、操作 Handle
  └─ runner thread：agentloop.run(...)
       └─ Handle、bot、dispatch、callbacks 全留在此 process
```

也就是：**MCP server 是執行中的 bridge，skill 是操作說明，plugin 只是成熟後的安裝與
散佈包裝。** 不需要先寫 Codex 自訂 UI，也不需要用 shell 去操控另一個 Python REPL。

這和 Pi 方案的分工相似，但 Codex 已原生支援 STDIO MCP，因此不需要先寫一層 TypeScript
extension 來管理 JSONL child protocol。MCP 已經負責 tool discovery、schema、request/result
與 tool policy；我們只需要實作 agentloop-aware 的 Python server。

## 整合目標

- 人繼續使用 Codex 的既有終端介面，由 Codex 觀察與操作一個 agentloop Round。
- agentloop 核心不認識 Codex；adapter 只組合公開的 `Handle`、callbacks 與
  `agentloop.threading.start()`。
- Python `Handle` 不跨 process；Codex 只持有 `round_id` 與可序列化 snapshot。
- 保留 Step 後、tools 前，以及 tools 後、下一個 Step 前的安全介入點。
- Codex 模型用的 MCP tools 和人透過 Codex 發出的要求走同一套公開協議。
- 第一版只管理一個 active Round，先驗證控制語意，不預先建立多-agent runtime。

## 非目標

- 不把 Codex thread／turn 當成 agentloop Round／Step；兩套狀態機彼此獨立。
- 不序列化 `Handle`、`bot`、Python callables、lock、`Reply` 或 exception object。
- 不用 Codex 的 `turn/interrupt` 模擬 agentloop 的 `pause()` 或 `end()`。
- 不讓遠端 controller 強制切斷正在執行的 agentloop model request 或 tool batch。
- 不在第一版提供 scheduler、持久化 Round、跨 Codex session reconnect 或多 client 控制。
- 不把 MCP、Codex sandbox 或 approval 誤稱為 agentloop tools 的 OS sandbox。
- 不同步 Codex 與 agentloop 的完整對話歷史，也不自動合併兩邊的 tool calls。

## 為什麼第一版選 MCP

Codex CLI、IDE extension 與 ChatGPT desktop 的 Codex host 原生支援 STDIO 與 Streamable HTTP
MCP server，並共用 Codex host 的 MCP 設定。STDIO server 由 command 啟動成 local process；
專案可在受信任 repository 的 `.codex/config.toml` 設定自己的 server。

這剛好符合 agentloop 的限制：真正的 Handle 留在 Python process，而 Codex 只呼叫結構化
tools。相較於自訂 subprocess protocol，MCP 已提供：

- tool 名稱、描述、input schema 與 result；
- server discovery 與啟動；
- 每個 server／tool 的 allow list、timeout 與 approval policy；
- Codex TUI 中既有的 tool 顯示與核准流程；
- 後續改成 Streamable HTTP service 的共同協議。

建議開發期使用 **project-scoped STDIO MCP**，設定 `required = true`，讓 server 啟動失敗時
立即看見問題，而不是讓 Codex 在缺少控制工具的情況下繼續。第一版不需要網路 listener、
OAuth、service discovery 或自訂 authentication。
