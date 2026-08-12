# Claude Code adapter 的風險與來源

[← 返回 Claude Code adapter 總覽](README.md)

## 風險

- **雙 agent 競爭**：Claude Code 與 agentloop 可能同時改同一工作樹。必須指定各自可用工具，
  或在 agentloop tool gate 人工批准副作用。
- **模型控制模型**：Claude 可能形成 `status → wait → resume → status` 循環。Claude 側應有限
  turns/actions，agentloop 側仍用 Limits 或自訂 callbacks。
- **權限錯層**：Claude Code 對 MCP `resume` 的允許，不等於 agentloop 內某個 shell tool 已
  經安全審查；兩層 policy 必須分開顯示。
- **過時修改**：observe mode 下 runner 會前進；所有 mutation 必須做 state/step/sequence
  compare-and-set。
- **orphan/強殺**：terminal crash、`/resume` session switch 或 MCP failure 可能切斷 server；
  第一版不保證恢復 parked Round。
- **protocol output**：stdout 日誌會損壞 stdio MCP；所有普通輸出導向 stderr。
- **資料外洩與 prompt injection**：Handle message/results 可能含不可信內容；不要讓其產生任意
  factory/import/patch 指令，Claude Code permissions 也不是 sandbox。
- **錯誤混淆**：Claude tool error、MCP transport/tool error、agentloop `state == "error"` 是
  三層不同錯誤，response schema 必須分開。
- **背壓**：event queue 要有限長度、單調 sequence 與 dropped count；不能因 Claude 忙碌而
  無限累積 snapshots。
- **版本漂移**：MCP tool naming、hooks schema、plugins 與 Channels 仍會演進，實作必須鎖版。

## 待決問題

1. 第一版 factory registry 是 adapter module 中的 dict，還是獨立設定檔？前者較簡單且不會
   把 arbitrary import 變成 MCP input。
2. `edit` 是固定 actions（`replace_tool_call`、`set_prompt`）還是有限 JSON Patch？固定 actions
   較容易驗證，JSON Patch 自由度較高。
3. `end`／`resume` 是否預設允許 Claude 自行呼叫，還是每次要求人類 permission？這是 Claude
   Code 入口 policy，不應寫進 agentloop 核心。
4. 是否要提供一個 MCP prompt 以及一個 project skill，還是初版只有 skill？兩者同時提供會
   有重複命令的可能。
5. teardown join timeout 到期時，是立即退出、留下警告，還是讓 MCP server 繼續等？三者不能
   同時保證操作完整與 UI 立即關閉。
6. 何時升級 HTTP service？判準應是「需要跨 Claude session 保留 Round」或「需要多 client」，
   不是為抽象而抽象。
7. 是否需要把 boundary snapshots 寫 event log？第一版可只保留 bounded memory queue，但 crash
   後無法還原。

## 官方資料

- [Claude Code：MCP](https://code.claude.com/docs/en/mcp)：local stdio server、project
  `.mcp.json`、tools/resources/prompts、plugin MCP lifecycle、stdio 不自動 reconnect 與 output
  limits。
- [Claude Code：Skills](https://code.claude.com/docs/en/slash-commands)：project skills、人工／
  模型 invocation、custom commands 已併入 skills，以及 skill hooks。
- [Claude Code：Hooks reference](https://code.claude.com/docs/en/hooks)：hook lifecycle、
  `PreToolUse`／`PostToolUse`／`SessionEnd`、decision control、MCP-tool hooks 與 async hooks。
- [Claude Code：Plugins](https://code.claude.com/docs/en/plugins)：plugin 可封裝 skills、hooks 與
  MCP servers，以及 `--plugin-dir` 開發流程。
- [Claude Code：Plugins reference](https://code.claude.com/docs/en/plugins-reference)：plugin
  結構、MCP server、hooks 與 Channels 的正式欄位。
- [Claude Code：CLI reference](https://code.claude.com/docs/en/cli-reference)：interactive CLI、
  `-p`、stream-json input/output、`--max-turns`、`--max-budget-usd` 與 MCP flags。
- [Claude Agent SDK：Python reference](https://code.claude.com/docs/en/agent-sdk/python)：
  `query()`、`ClaudeSDKClient`、interrupt、programmatic hooks、`@tool` 與 in-process
  `create_sdk_mcp_server()`。
- [Claude Agent SDK：Sessions](https://code.claude.com/docs/en/agent-sdk/sessions)：Claude session
  的 continue/resume/fork 語意，以及 conversation persistence 與 filesystem state 的差別。
- [Claude Code：Channels](https://code.claude.com/docs/en/channels) 與
  [Channels reference](https://code.claude.com/docs/en/channels-reference)：MCP event push 的
  research-preview 能力、存活 session 限制與部署條件。
- [Anthropic `claude-code` repository](https://github.com/anthropics/claude-code)：官方公開 repo、
  changelog 與 plugin examples；本文未依賴未公開內部實作。
