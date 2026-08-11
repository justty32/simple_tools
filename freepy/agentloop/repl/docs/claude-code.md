# 以 Claude Code 操作 agentloop

本文記錄把 Claude Code 當作 agentloop 進階互動入口的初步設計。這一階段只比較官方提供的
擴充方式、定義 process／thread 邊界並提出第一版架構，不實作 adapter。

Claude Code API 與文件最後核對日期為 **2026-08-11**。本文只使用 Anthropic 官方文件與
官方 repository；正式實作時仍應鎖定 Claude Code／Agent SDK 版本並重新核對 schema。

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

## 為什麼 MCP 是第一版主入口

Claude Code 官方把 MCP 定位為加入 custom tools 的入口，並原生支援 local stdio server。
它會以 child process 啟動 server，設定 `CLAUDE_PROJECT_DIR`，並把 server tools 放進 Claude
可用的工具集合。這恰好符合 agentloop 的限制：

- MCP server 是 Python process，可以在 process 內真正持有 `Handle`。
- server 內可建立一條 runner thread；MCP request handler 則扮演 controller。
- Claude Code 只看 JSON-compatible tool input/output，不需要也不應拿到 Python object。
- 人繼續使用 Claude Code TUI，不必另外造介面。
- MCP 的 tool schema、錯誤回傳、permission 與 discovery 已由 Claude Code 處理。

這裡仍有 IPC，但不必自行發明 framing、request id 與 stdout parser。stdio stdout 必須專供
MCP protocol；bot、tool 與 server 診斷應寫 stderr 或檔案，否則仍會污染 transport。

### 建議的 MCP tools

第一版維持一個 active Round，提供：

- `start`：用具名且預先允許的 factory 建立 `bot`、`dispatch`、
  `Handle(auto_finish=False)` 與 callbacks，再啟動 runner。
- `status`：在 Handle lock 下製作精簡 snapshot，立即返回。
- `wait`：等待 state／boundary event；一定要有 timeout 與 `after_sequence`，不可永久占住
  Claude 的 tool call。
- `pause`：呼叫 `handle.pause()`。
- `resume`：先原子套用選定修改，再呼叫 `handle.resume()`。
- `end`：呼叫 `handle.end(reason="claude-code")`。
- `edit`：套用明確定義的 JSON operations；不接受 arbitrary Python execution。
- `detail`：依 step、call id 或 event sequence 分頁取得被截斷的 message/result。

`start` 不應接受使用者輸入的任意 module path。比較穩定的方式是 adapter 啟動時由設定指定
允許的 factory registry，MCP input 只選名稱。`edit` 初版可允許 `prompt`、`images`、
`ask_options`、`tool_calls`、`tool_results`、`auto_finish` 等可序列化欄位；`dispatch` 中真正的
callable 仍只能由 Python factory 建立。

所有 mutation 都應帶 `expected_step`、`expected_state`，最好再帶 boundary event sequence。
若 snapshot 已過時，server 拒絕修改並回傳目前版本，避免 Claude 根據 Step N 的資料改到
Step N+1。

### Snapshot 與 event

遠端 snapshot 只暴露有意義且可序列化的報告，例如：

```text
round id、state、step、event sequence、boundary type
message、tool_calls、tool_results、tool_log 摘要
input/output/cached-input tokens
stop、end_reason、err 的安全文字表示
runner 是否仍存活、是否有 pending pause/end request
```

大型 message 與 tool result 應截斷，附 `truncated`、原始大小及可傳給 `detail` 的 reference。
Claude Code 會限制及警告過大的 MCP tool output，因此不要把整個 Handle/history 每次塞回
context。

## Handle、thread 與 process 邊界

```text
Claude Code                         Python MCP server
-----------                         -----------------
呼叫 MCP tool          --------->   MCP handler/controller
                                    真正的 Handle
                                    runner = start(...)
                                        │
                                        └─ 唯一 runner thread
status/edit/pause/...   --------->   controller 與 runner 共享 Handle
JSON snapshot/result    <---------   Python object 留在 server process
```

一個 Handle 仍是 one-shot，只能對應一個 Round 及一條 runner thread。Claude Code session 的
`/resume`、branch 或 SDK fork 只複製／恢復 Claude 的 conversation，不會 fork Python
Handle，也不會讓已死的 stdio server 恢復記憶體狀態。

MCP handler 與 runner thread 做複合修改時使用 `with handle.edit()`。若 MCP framework 使用
async event loop，阻塞的 `wait_for_state()`／`runner.join()` 應移到 worker thread 或採短 timeout
輪詢，不能阻塞整個 protocol loop。

## agentloop callbacks：observe 與 gate

Claude Code 在另一個 process，不能成為 agentloop 的同步 callback。adapter callback 只能做：

- **observe mode**：建立 immutable snapshot、放入有上限的 Python event queue，回
  `CONTINUE`。Claude 稍後才看到通知，runner 可能已進入下一 operation。
- **gate mode**：enqueue snapshot 後回 `PAUSE`。runner 在 callback 結束後安全停住，Claude
  再用 MCP `status`／`wait` 查看，透過 `edit` 修改，最後 `resume` 或 `end`。

如果目標是批准或改寫 agentloop 的 tool calls，必須讓 `after_step` gate 回 `PAUSE`；如果要
改寫 tool results，必須讓 `after_tools` gate 回 `PAUSE`。第一個端到端範例建議預設：

```text
after_step = gate（展示 tools 執行前審核）
after_tools = observe（避免每批結果都強迫停住）
```

callback 在 runner thread 且持有 Handle 的 `RLock` 執行，因此它只應複製 snapshot、enqueue
並迅速返回。不可在 callback 裡等待 Claude/MCP response，也不可建立需要存取同一 Handle
的 child thread 後當場 `join()`。

一般 MCP server 不會無條件把 boundary event 主動變成 Claude 的新 turn；第一版使用有
timeout 的 `wait` tool 或由 skill 指示 Claude 在必要時查詢。Claude Code Channels 雖能讓 MCP
server push event 進活著的 session，但目前是 research preview、有驗證與部署限制，而且會
讓 observe event 自動驅動另一個 agent turn；不應成為第一版依賴。

## pause、resume、end 的映射

| Claude/MCP action | Python 行為 | agentloop 語意 |
|---|---|---|
| `pause` | `handle.pause()` | 立即接受要求；完成當前 Step+callbacks 或 tool batch+callbacks 後才停 |
| `resume` | `with handle.edit(): ...` 後 `handle.resume()` | 只喚醒 waiting/paused，或取消尚未生效的 pause |
| `end` | `handle.end(reason="claude-code")` | parked 時立即完成；running 時等下一安全邊界，不強殺 operation |
| `status` | lock 下產生 snapshot | 只觀察，不改控制流 |
| `edit` | `with handle.edit(): ...` | 修改不會自行喚醒，也不代表 Round 未完成 |

Claude Code 的 Ctrl-C、Agent SDK `interrupt()` 與 agentloop `pause()`／`end()` 控制的是不同
agent loop。前兩者中止 Claude Code 當前 turn，不會自動安全暫停 Python runner；反過來，
agentloop parked 也不代表 Claude Code turn 被中止。

## Skills／commands 的角色

Claude Code 現在把 custom commands 併入 skills；project skill 放在
`.claude/skills/<name>/SKILL.md`，人可用 `/name` 呼叫，模型也可依 description 自動載入。

建議提供一個薄的 `agentloop` skill，內容只描述：

- 先 `status`，依 state 選擇下一個 MCP action；
- gate 停住時，先核對 step/sequence，再 edit/resume；
- 不把 Claude Code permissions 誤當 agentloop tool approval；
- 不重複 start，不對 completed/error Handle resume；
- 何時應把決定交回人類，尤其是 `end` 與有副作用的 tool-call rewrite。

Skill 是 prompt-based workflow，不是可靠的 policy enforcement，也不能持有 Handle。若某操作
只准人觸發，可用 skill frontmatter 限制 model invocation，並另外用 Claude Code MCP tool
permissions 限制模型是否能直接呼叫 `end`／`edit`。

MCP server 也可以曝光 prompts；Claude Code 會把它們顯示成
`/mcp__servername__promptname` command。第一版可以先用 project skill 取得較好名稱與配套
文件；若未來希望 adapter 在其他專案安裝後自帶 workflow，再同時提供 MCP prompt 或 plugin
skill。

## Hooks 的角色與限制

Claude Code hooks 是 Claude Code 自己的 lifecycle 邊界，例如 `PreToolUse`、`PostToolUse`、
`Stop`、`SessionStart`、`SessionEnd`。它們可執行 command、HTTP、prompt、agent 或已連線的
MCP tool，部分事件可以 block、deny 或修改 Claude Code tool input/output。

它們不等同 agentloop callbacks：

- `PreToolUse` 只看到 Claude Code 將要執行的 tool。agentloop runner 內部模型要求的 Python
  tool calls 是 Handle 資料，不是 Claude Code tool call，hooks 看不到。
- 可以對 `mcp__agentloop__end`／`edit` 等 MCP tools 設 `PreToolUse` hook 或 permission rule，
  控制 Claude 是否可操作 Handle；這是入口權限，不是 agentloop boundary gate。
- `SessionEnd` 沒有 decision control，不能阻止 session 結束；預設 timeout 也很短。它可透過
  MCP tool best-effort 呼叫 `end`，但不能保證長時間 operation 完成與 join。
- MCP-tool hook 要求 server 已連線；`SessionStart` 常早於 MCP server 連線，因此不適合依賴
  它建立 Round。
- async hook 不會阻塞 Claude，完成結果只在仍活著的 session 中送達，也不提供 agentloop
  同步 gate 所需的保證。

所以第一版不需要 hooks 才能運作。等要限制「Claude 不得自行 end」或在 session teardown
做 best-effort cleanup 時再加，並清楚標為 Claude Code policy／cleanup，而非核心控制流。

## Lifecycle 與 teardown

第一版把 stdio MCP server 與 Claude Code session 視為同生命週期，但不要把這理解成持久化
保證：官方文件說 plugin MCP servers 會在 session startup 自動連線，也明確指出 stdio
server crash 後不會自動 reconnect。故設計應假定連線一斷，記憶體 Handle 就不可恢復。

正常關閉次序：

1. MCP server 收到明確 `shutdown`／transport EOF，或 optional `SessionEnd` cleanup action。
2. 若 Handle 尚活著，呼叫 `handle.end(reason="claude-code-disconnect")`。
3. 用有限 timeout 等 `runner.join()`。
4. 若 runner 尚在 model request/tool batch，記錄「safe end 尚未抵達邊界」。
5. server process 最終若被 Claude Code 或 OS 終止，視為不可抗力；不能聲稱 operation 已安全
   收尾。

不要讓 server 為了等一個無限期卡住的 Python tool 而阻止 Claude Code 永遠退出。agentloop
沒有 unsafe cancellation，因此「立即關掉 process」和「保證已開始 operation 完整提交」
無法同時成立。

若需求變成 Claude `/resume`、Claude process restart 後仍保留活著的 Round，stdio child
就不夠。那時應升級成獨立的 localhost HTTP MCP service 或其他 service IPC，加入 Round
registry、authentication、租約、重連與持久化；不是把 Handle 序列化給新 process。

## 可行方案比較

| 方案 | 優點 | 問題 | 結論 |
|---|---|---|---|
| **互動式 Claude Code + local stdio MCP + skill** | 保留完整 TUI；Python server 直接持有 Handle；使用官方 custom-tool 通道；最少自訂層 | 要管理 child/runner lifecycle；遠端只看可序列化狀態；stdio crash 不 reconnect | **推薦第一版** |
| 只做 skill／command，透過 Bash 操作 Python REPL | 幾乎零 adapter | 每次 shell 是獨立 process；REPL/tmux 解析脆弱；無可靠 Handle ownership 或 callback gate | 不採用 |
| Claude Code hooks + scripts | 有 deterministic lifecycle trigger，也能限制 Claude tools | hooks 控制的是 Claude Code，不是 agentloop；command hook process 無法取得既有 Handle | 只作 policy/cleanup 配角 |
| 打包成 Claude Code plugin | 可一起配送 skill、MCP config、hooks；啟用時自動啟動 MCP server | 太早打包會把未穩定 protocol 固化；仍是同一個 MCP 架構 | 第二階段 |
| Python Agent SDK + in-process SDK MCP tools | Python app、Handle 與 custom tool callbacks 同 process；`ClaudeSDKClient` 支援多 turn、hooks、interrupt | Python app 變成 top-level UI；失去原生 interactive TUI；interrupt 仍不是 Handle pause | 自訂產品／automation 很合適，不是第一個人類入口 |
| `claude -p` + stream-json subprocess | 官方 CLI 支援 stream-json input/output、turn/budget limits | 要自己做 host UI、stream lifecycle 與錯誤處理；比 Agent SDK 低階 | CI／一次性自動化 |
| 獨立 HTTP MCP service | 可跨 Claude session、支援多 client 與 reconnect | 立即需要 auth、registry、租約、衝突與 persistence policy | 有長壽 Round 需求後再做 |
| Claude Code Channels 推送 boundary event | 可主動把外部事件送入活著的 session | research preview、平台限制；事件會驅動 Claude turn，增加循環與成本風險 | 暫不依賴 |

## 第一個端到端範例應驗證什麼

後續第一個實作／example 應刻意小：

1. Claude Code 透過 MCP `start` 啟動 `auto_finish=False` Round。
2. `after_step` gate 看見一個 tool call，enqueue snapshot 並回 `PAUSE`。
3. Claude `wait/status` 取得同一 step/sequence，說明風險。
4. 人允許後，Claude 用具 expected version 的 `edit` 改寫 call，再 `resume`。
5. `after_tools` observe 記錄結果；Round 下一 Step 自然進入 `waiting`。
6. Claude status 後由人決定追加 prompt/resume，或 `end`。
7. server join，回報 completed、stop/end_reason 及 token counters。

這一條流程會同時驗證 Handle 不跨 process、MCP schema、stale-write protection、callback gate、
安全 pause/resume/end 與正常 teardown。

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
