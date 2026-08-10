# freepy 接入既有 coding agent：可行性報告

日期：2026-08-10

## 結論

可行，但不是「freepy 從此不要自己的介面」。較準確的產品形狀是：

- **freepy 自己擁有 control plane**：管理大量 bot、Round、Step、endpoint queue 與團隊；原生介面是 Python API／REPL、shell，未來可投影成 Plan 9 式檔案 namespace。
- **借用現成 agent 的人機介面**：Claude Code、Codex、Pi 用來觀察或操作少數領頭／窗口 agent，不承擔成千上萬個背景 bot 的總控 UI。

因此不必重做單一 agent 的聊天、diff、approval、terminal；但仍要做適合大規模編排的原生介面。規模與 scheduler 判斷另見 [`CONTROL-PLANE.md`](CONTROL-PLANE.md)。

眼前順序不變：先照 [`agentloop/PLAN.md`](agentloop/PLAN.md) 完成 Round／Step 與 thread-safe Handle，交使用者審核。adapter 與大規模 scheduler 都在其後；不反過來改 llmkit 根基，modelcards 仍等審核。

## 先分清誰擁有 loop

```text
A. host-owned loop
   Claude/Codex/Pi 的 UI + 模型 + loop -> adapter -> freepy capability

B. freepy-owned Round
   host UI -> control adapter -> agentloop -> llms -> proxy -> thinking engine
```

A 適合讓窗口 agent 經 MCP／Pi extension 使用 tooljson 能力。B 才會執行 freepy 的 Round；host 只做觀察、追加指令與控制。**同一件工作不可讓兩邊都暗中擁有 loop**，否則會有雙重工具執行、兩套 history、兩層 approval 和無法解釋的預算。

MCP 是 host adapter，不是模型 endpoint；它不取代 `llms -> proxy -> OpenAI-compatible endpoint`。

## 現況與真正價值

freepy 已接近 headless core：`llms` 是一次模型請求、`tooljson` 生出 schema＋dispatch、`agentloop.run()` 是一個 Round，`Handle` 是控制把手。`shells/pi.py`、`shells/claude.py` 也已用 `exec` 借用原生 TUI、Ctrl-C 與退出碼，證明「借 UI」這條路可行。

目前 read/write/edit/shell 與 coding agent 內建能力高度重疊；把四個基本工具再包一次 MCP，價值有限。值得適配的是：

- 讓窗口 agent 使用 **tooljson 所描述的額外能力**，而不必知道 Python 實作。
- 讓窗口 agent 觀察／控制 **freepy-owned Round 或團隊**。
- 讓各入口共用同一組 root、limits、tool policy 與結果語意。

## 建議分層

```text
Python REPL / shell      Claude + Codex          Pi
       |                     MCP             TS extension
       +----------------------+------------------+
                              |
                     public Python operations
                       /                    \
          control plane / scheduler      agentloop Round
                   |                    /             \
             bot/team records      tooljson         llms -> proxy
```

Python operations 是唯一真實實作；REPL、shell、MCP、Pi extension、未來的 filesystem／HTTP 都只映射同一組 typed operations，不各自重寫 loop 或狀態機。

## 適配面評估

| 接法 | 可行性 | 判斷 |
|---|---:|---|
| Python library／REPL | 高 | **原生核心介面**；適合開發、除錯與管理，先收斂 stable operations |
| shell + JSON/JSONL | 高 | **原生自動化介面**；適合 script、pipe、批次與遠端 shell |
| MCP stdio | 高 | Claude Code/Desktop、Codex CLI/IDE/Desktop 的第一個 adapter；適合窗口 agent |
| MCP Streamable HTTP | 高 | 遠端／常駐服務；有 auth 與部署成本，本機第一版不用 |
| Pi extension/package | 高 | Pi 首選；薄 TypeScript glue 呼叫 Python，別在 TS 重做 core |
| Pi RPC | 高，方向不同 | 適合外部程式嵌入／控制 Pi，不是 Pi 呼叫 freepy 的首選 |
| localhost HTTP | 中高 | 多 client、跨 process 才需要；先 loopback＋token |
| `prompt_toolkit` | 中 | REPL 不夠用時補 completion/status；不是 scheduler 前置條件 |
| Plan 9 filesystem | 未來 | typed operations 穩定後的原生 projection，不提前改根基 |
| tmux module | 放棄 | 已拍板不做 |

官方現況支持這個排序：Codex CLI、IDE extension 與 ChatGPT desktop 的 Codex host 共用 MCP 設定，支援 stdio 與 Streamable HTTP；Claude Code 同樣支援本地 stdio 與 HTTP，另能使用 tools/resources/prompts。Pi 明說 core 不內建 MCP，而把這類能力留給 extensions/packages；其 RPC 是嚴格 JSONL，官方也提供 Python subprocess 範例。

## 長 Round 不要是一個阻塞 MCP call

MCP 有 progress、cancellation 與 experimental tasks；Claude Code 也能把長 tool call 背景化。但這些能力 optional／host-specific，timeout、session 結束與重連後狀態仍要由 freepy 自己定義，不能成為 Round 正確性的基礎。

MCP 適合兩類短操作：

1. capability mode：列出／呼叫選定的 tooljson capability；host 擁有 loop。
2. control mode（後做）：`round_start` 立即回 id；另以 status、instruction、pause/resume/stop、result 操作背景 Round。

Round 應在 freepy worker 中繼續，不能綁死於單一 MCP request。大量 bot 的 queue、lease、恢復也不該藏在 Claude/Codex/Pi session 裡。

## 最小落地順序

1. **完成 agentloop 並審核**：維持 `run(bot, dispatch, ...)`，不為 adapter 提前加 service。
2. **收斂 public Python operations**：先支援 REPL；操作回 typed data，不回只能供人讀的字串。
3. **補 shell**：少量 `list/status/tell/stop/attach`，預設人讀，`--json` 穩定可 pipe。
4. **做 MCP stdio vertical slice**：讓 Claude Code 與 Codex 對同一個窗口 agent 執行 status＋tell；不先暴露整個長 Round。
5. **做 Pi project-local extension**：同一 operation 經 Python child／本機 API 呼叫，再決定是否包成 Pi package。
6. **有多 bot 實證才抽 scheduler/service**；有多 client 才加 HTTP；typed operations 穩定後才做 filesystem projection。

## 必須守住的邊界

- adapter 把 host 授權的 workspace、actor 與 team scope 綁進 freepy；不可讓模型隨 call 指定任意 root／agent。
- host 決定是否發 call；freepy 仍 enforce root、allowlist、limits。不能只信外部 UI 的 approval。
- stdio stdout 只走 protocol，log 走 stderr；HTTP 預設 loopback，使用不可猜 token，不直接公開 LAN。
- start 要有 request id／冪等語意；status/result 可重讀；stop 是 cooperative，不能聲稱撤銷已開始的 endpoint request 或工具副作用。

## 決策

**採用「freepy 原生 control plane + 薄 host adapters」。**不另做單 agent chat/TUI；Python REPL 與 shell 是近期原生介面，既有 coding agent 是窗口介面，Plan 9 read/write 是未來的大規模 projection。

## 一手資料

- [OpenAI：Codex / ChatGPT desktop 的 MCP 支援與共用設定](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)
- [Anthropic：Claude Code MCP](https://code.claude.com/docs/en/mcp)
- [Claude Desktop：本地 MCP server](https://support.claude.com/en/articles/10949351-getting-started-with-local-mcp-servers-on-claude-desktop)
- [Pi：extensions](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/extensions.md)
- [Pi：RPC mode](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/rpc.md)
- [Pi：core 不內建 MCP](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/usage.md#design-principles)
- [MCP：架構與 transports](https://modelcontextprotocol.io/docs/learn/architecture)
- [MCP：experimental tasks](https://modelcontextprotocol.io/specification/2025-11-25/basic/utilities/tasks)
