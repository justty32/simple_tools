# 以 OpenAI Codex 操作 agentloop

本文記錄把 OpenAI Codex 當作 agentloop 進階互動入口的初步設計。這一階段只比較官方
提供的擴充與程式化介面、定義 process 邊界並提出第一版架構，不實作 adapter。

官方 Codex manual 與各介面最後核對日期為 **2026-08-11**。

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

## MCP server 的最小公開工具

建議使用多個明確工具，而不是一個帶任意 `action` 的萬用工具。這讓 schema、說明與
Codex 的 per-tool approval policy 都更清楚：

- `agentloop_start`：用具名 Python factory 建立 `bot`、`dispatch`、Handle 與 runner。
- `agentloop_status`：取得精簡 snapshot，不等待狀態改變。
- `agentloop_wait`：從指定 event sequence 後等待 boundary／state event，必須有 timeout。
- `agentloop_pause`：呼叫 `handle.pause()`；回覆「已接受要求」，不謊稱已經 paused。
- `agentloop_edit`：在 `handle.edit()` 中套用明確、可驗證的修改。
- `agentloop_resume`：呼叫 `handle.resume()`。
- `agentloop_end`：呼叫 `handle.end(reason="codex")`。

`start` 不接受 pickle、任意 Python expression 或任意 module import path。由應用程式設定檔
列出可用 factory 名稱，再由 server 查表。跨 process 的「Handle 全公開」只能落成經過設計
的 JSON representation；它不可能無損等同 Python REPL 的任意物件存取。

第一版 `edit` 可先支援固定 operations：

- 設定／清空 `prompt`、`images`、`ask_options`；
- 取代、刪除或新增一個 tool call；
- 取代或刪除一筆 tool result；
- 設定 `auto_finish`；
- 調整可序列化的 `dispatch` tool enabled set，而不是傳入 callable。

每個 mutation 都應帶 `expected_state`、`expected_step` 與 snapshot `sequence`。任一不符就拒絕
修改，要求 Codex 重新 `status`／`wait`，避免它根據過時 snapshot 改到下一個 boundary。

## Snapshot 與事件

一般 snapshot 只回傳安全且有上限的報告：

- `round_id`、單調遞增 `sequence`、`state`、`step`；
- `message`、`tool_calls`、`tool_results`；
- `tool_log` 摘要、`calls`、`used`；
- `input_tokens`、`output_tokens`、`cached_input_tokens`；
- `stop`、`end_reason`、`finish_reason`；
- `err` 的安全文字形式。

大型 message、arguments 與 results 應截斷並標記原始大小；另以帶分頁／field selector 的
detail 查詢取得。不要把 exception traceback、環境變數或任意 Python repr 直接送入 Codex。

Python callbacks 可以把 immutable snapshot 放進有上限的 queue。`agentloop_wait` 等待 queue
中 `sequence > after_sequence` 的事件，timeout 後正常回傳「暫無新事件」，不要讓一個 Codex
MCP tool call 永久掛住。Queue 滿時保留最新狀態並回報 dropped count。

MCP tool result 是 request/response，不應假設 Codex 模型會主動接收任意 server push。因此
事件 queue 是讓下一次 `wait`／`status` 讀取，不是另一套即時 callback transport。

## Callback：observe 與 gate

Codex 在另一個 process，不能成為 agentloop 的同步 callback。若 callback 回 `CONTINUE`，
Codex 看到通知時 runner 可能已開始下一個 operation。

因此保留兩種模式：

- **observe**：callback enqueue snapshot 後回 `CONTINUE`。適合記錄與事後觀察。
- **gate**：callback enqueue snapshot 後回 `PAUSE`。runner 停在安全邊界，Codex 再檢查、
  修改並 `resume()` 或 `end()`。

如果要批准或改寫模型剛產生的 tool calls，`after_step` 必須採 gate；如果要改寫 tools 的
results，`after_tools` 必須採 gate。Callback 本身只 enqueue 並快速返回，不等待 Codex 回應，
也不建立會操作同一 Handle 後又被 callback `join()` 的 thread。

第一個端到端範例建議預設：

```text
after_step = pause
after_tools = pause
auto_finish = false
```

這雖然較慢，卻能一次驗證兩個介入點。證實可靠後，常態可改為
`after_step=pause, after_tools=continue`，或完全 observe。

## pause、resume、end 的精確映射

| MCP tool | Python 行為 | 真正語意 |
|---|---|---|
| `agentloop_pause` | `handle.pause()` | 立即接受要求；Step+callbacks 或整批 tools+callbacks 完成後才 paused |
| `agentloop_edit` | `with handle.edit(): ...` | 原子修改資料；不會自行喚醒 runner |
| `agentloop_resume` | `handle.resume()` | 喚醒 waiting／paused，或取消尚未生效的 pause |
| `agentloop_end` | `handle.end(reason="codex")` | parked 時立即完成；running 時等下一個安全邊界 |
| `agentloop_wait` | 等 Handle state／event queue | 只同步觀察，不改控制流 |

MCP response 要分開表示 `accepted` 與目前 `state`。例如 `pause()` 回 `True` 只表示要求已被
接受；需要可靠修改 tool calls 時，Codex 必須再 `wait` 到 `paused`。

agentloop callback 回 `END`、Limits callback 回 `END` 與 Codex 呼叫 `agentloop_end` 最後都會
結束 Round，但起因不同。Snapshot 應保留 `stop` 與 `end_reason`，不要全部顯示成「Codex
停止」。

## 不要混淆 Codex 的控制流

Codex app-server 本身有 thread、turn、item，也有 `turn/steer` 和 `turn/interrupt`：

- `turn/steer` 是把輸入追加到正在進行的 **Codex turn**；
- `turn/interrupt` 是要求取消 **Codex turn**，完成狀態會是 `interrupted`；
- `agentloop_pause/resume/end` 控制的是 Python process 裡的 **agentloop Round**。

中斷 Codex turn 不會自動暫停 agentloop；暫停 agentloop 也不會中斷 Codex。若人中斷 Codex
時 agentloop 仍在 running／waiting，MCP server 仍必須依自己的 lifecycle policy 管理它。
第一版不要暗中把兩者綁在一起。

## Process 與生命週期

Codex 只持有 MCP connection；Python MCP process 持有真正 Handle。Server 內部可用一把
registry lock 保護 active Round reference，Handle 自己的複合修改仍使用 `handle.edit()`。
即使 Codex 通常循序呼叫 tools，也不要把「永遠只有一個同時 request」當作正確性前提。

第一版採一個 MCP process、一個 active Handle、一條 agentloop runner thread：

1. MCP initialize 後 server 尚未建立 Round。
2. `agentloop_start` 建立 `Handle(auto_finish=False)`，註冊 callbacks，再啟動 runner。
3. Codex 以 status/wait/control tools 操作它。
4. 正常結束時 `end()`（若需要）後 `runner.join()`，再清除 registry。
5. MCP transport 收到 EOF、shutdown 或 process signal 時，對 active Round 呼叫 `end()`，
   有限時等待 `join()`，並把診斷寫到 stderr。

agentloop 沒有 unsafe cancellation。若 model request 或 Python tool 永久卡住，`end()` 也只能
等待安全邊界。為避免 STDIO parent 關閉後留下不能退出的 child，bridge runner 可明確使用
`daemon=True`，但文件與 log 必須承認：shutdown timeout 後 Python process 結束會切斷 operation，
不保證 callback 或 history 收尾。正常路徑仍一律 `end()` 加 `join()`。

官方文件確認 STDIO MCP 是由 command 啟動的 local process，但沒有承諾它是跨 Codex client
重啟的 durable Round host。因此第一版把 MCP process 消失視為 Handle 消失；不要只把
`round_id` 寫進 Codex thread 後假裝可重連。

若未來需要跨 Codex session 保留 Round、多個 Codex clients 共用或 server reload 後重連，
再把同一套 MCP tools 搬到獨立的 Streamable HTTP service，加入 authentication、Round registry、
lease／controller ownership、持久化與 conflict policy。這是 runtime 升級，不是 agentloop
核心修改。

## 方案比較

| 方案 | 優點 | 問題 | 結論 |
|---|---|---|---|
| **Codex TUI + STDIO MCP + skill** | 使用現成 UI、tools 與 approvals；Python process 持有 Handle；協議標準；可逐步包成 plugin | 要實作 snapshot、event queue 與 lifecycle；無任意 Python object 存取 | **推薦第一版** |
| 只寫 skill | 最少內容；能教 Codex 執行既有 scripts | Skill 是 instructions/resources，不是活的 process bridge；無法持有 Handle 或提供可靠同步工具 | 只作 MCP 的操作說明 |
| Plugin（skill + MCP） | 一包安裝與散佈；可以同時帶 workflow 與 server wiring | 不會消除 process 邊界；開發早期增加 manifest、安裝與版本管理 | 驗證後封裝 |
| Python Codex SDK + agentloop | 同一 Python host 可同時管理 Codex app-server client 與 agentloop Handle；適合程式化 orchestration | 沒有現成 Codex TUI；host 要自己提供 UI、approval handling 與兩套 lifecycle | 自訂產品／Agent Machine 再用 |
| 直接使用 Codex app-server | 最完整 thread/turn/item events、approvals、streaming；適合深度 client | 協議面大；自建 client；WebSocket transport 官方仍標示 experimental/unsupported | 不是第一個互動入口 |
| `codex exec --json` subprocess | 腳本與 CI 很簡單；JSONL events、structured output、session resume | 每次是 headless job；沒有自然的長期 Handle controller；若仍靠 MCP，核心方案其實還是 MCP | 批次任務，不作主入口 |
| Codex shell 操作 Python REPL／tmux | 幾乎不用 adapter | parsing 脆弱、無 schema/correlation、容易 orphan、無可靠 callback gate | 只供人工試驗 |
| 把 Codex CLI本身開成 MCP server給 agentloop | 官方支援 Codex 作為其他 orchestrator 的 specialist | 控制方向相反：是 agentloop/Agents SDK 呼叫 Codex，不是 Codex 操作 agentloop | 可供未來 Agent Machine |

## Plugin 與 skill 應該做到哪裡

Pi extension 可以直接註冊 TUI command、custom tool 與 UI；Codex 的對應物不是單一 extension
API，而是不同責任的組合：

- **MCP server**：live state、控制 actions、schema、authorization 與 process boundary。
- **Skill**：可重複的操作順序，例如「先 status，再 pause+wait，帶 expected sequence edit，
  最後 resume」；也記錄不得混淆 Codex interrupt 與 agentloop end。
- **Plugin**：把 skill、MCP server manifest 與必要 metadata 包成可安裝單位。

所以第一版先在 repository 放 project MCP config、server script 與 project skill，方便直接改與
測試。等 tool schema、factory contract 與 lifecycle 穩定後，再建立 `.codex-plugin/plugin.json`
與 `.mcp.json` 封裝。Plugin 是發行階段，不應承擔 agentloop runtime 語意。

Skill 至少應要求 Codex：

1. 控制前先取得最新 status。
2. 修改 tool calls/results 前先 pause 並確認真的進入 paused。
3. edit 一律帶 expected state/step/sequence；衝突就重讀，不盲目重試舊 patch。
4. 修改後只有明確 resume 才繼續。
5. 任務不再繼續時呼叫 end，並確認 completed/error。
6. 不同時讓 Codex coding tools 與 agentloop tools 寫同一批檔案。
7. 限制 status→wait→status 的循環次數、等待時間與 agentloop Limits。

## 權限與安全

MCP tool approval 是 Codex host 的 policy，不是 agentloop 的安全邊界。尤其 `resume` 可能放行
尚未執行的 Python tool calls，應視為可能產生副作用的控制 action。

初版建議：

- `status`、短 timeout 的 `wait` 可自動執行；
- `start`、`edit`、`resume`、`end` 預設要求核准；
- `pause` 可自動或要求核准，視使用者是否接受干擾 Round；
- MCP server 的 `enabled_tools` 只開第一版所需工具；
- factory、可用 dispatch tools 與檔案 roots 由 Python 應用層 allowlist；
- Codex 與 agentloop 使用同一工作樹時，必須指定單一 writer 或使用 worktree 隔離。

Codex sandbox 約束 Codex 自己的 command/file operations；STDIO MCP server 及其 Python tools
的實際權限還取決於 host、MCP 啟動方式與 OS 環境。不要因 Codex 顯示 workspace-write 就假設
agentloop dispatch 自動受到相同限制。

## 預計文件、scripts 與 examples

後續若實作，可採：

```text
repl/
├── docs/
│   └── codex.md
├── scripts/
│   └── codex_mcp.py             # MCP server、registry、snapshots、events、shutdown
├── skills/
│   └── agentloop-controller/
│       └── SKILL.md             # Codex 的可靠操作流程
└── examples/
    ├── codex_minimal_factory.py # 建立 bot/dispatch 的具名 factory
    ├── codex_project_config.toml
    └── codex_gate_tools.md      # after_step/after_tools gate 範例
```

第一個驗收場景：啟動 `auto_finish=False` Round，after-step gate 暫停；Codex 讀取並改寫一個
tool call，resume；after-tools gate 再檢查結果；最後 end/join。它能一次驗證 MCP schema、
過時修改防護、兩個 callback boundary 與正常 teardown。

## 待決問題

1. Python factory 採固定 registry、`module:function` allowlist，還是由 server entrypoint 直接
   import 應用程式？固定 registry 最容易限制。
2. `agentloop_edit` 採固定 operations 或 JSON Patch 子集？固定 operations 適合第一版；成熟後
   可加入受限 JSON Patch。
3. 第一版是否允許 Codex 模型自己 `resume`／`end`，或這兩項每次都要人核准？這是 host
   policy，不應寫進 agentloop 核心。
4. Gate 預設是 only `after_step`，還是兩個 boundary 都 pause？展示版建議兩者都 pause。
5. MCP shutdown timeout 後是讓 daemon runner 隨 process 結束，還是留下警告並等待人工處理？
   agentloop 無法同時保證立即退出與 operation 完整。
6. 何時升級 Streamable HTTP？應以跨 session persistence、多 client 或 remote host 的實際需求
   為門檻。
7. Codex 與 agentloop 是否允許共用寫入工具？如果允許，誰負責 worktree lock 與衝突復原？

## 官方資料

以下均為 OpenAI 官方資料；本文先以當日重新取得的 Codex manual 定位並精讀相關章節：

- [Codex manual](https://developers.openai.com/codex/codex-manual.md)：官方 Codex 綜合手冊；
  本文核對 Noninteractive and Programmatic Interfaces、MCP、Skills 與 Plugins 章節。
- [Model Context Protocol](https://learn.chatgpt.com/docs/extend/mcp.md)：Codex 支援的 STDIO／
  Streamable HTTP transports、project config、tool allowlist、timeout 與 approval mode。
- [Codex App Server](https://learn.chatgpt.com/docs/app-server.md)：JSON-RPC transports、
  thread/turn/item lifecycle、stream events、approvals、steer 與 interrupt。
- [Codex SDK](https://learn.chatgpt.com/docs/codex-sdk.md)：TypeScript 與 Python SDK；Python SDK
  控制 local app-server，適合自訂 host 與程式化 orchestration。
- [Non-interactive mode](https://learn.chatgpt.com/docs/non-interactive-mode.md)：
  `codex exec`、JSONL events、structured output、sandbox 與 session resume。
- [Skills](https://developers.openai.com/plugins/concepts/skills)：skill 是 workflow instructions 與
  resources，MCP server 提供 live data 與 controlled actions。
- [Plugins](https://learn.chatgpt.com/docs/plugins)：Codex plugin 可組合 skills 與 MCP servers，
  Codex CLI 提供 plugin browser。
- [Build plugins](https://learn.chatgpt.com/docs/build-plugins)：`.codex-plugin/plugin.json`、
  bundled skill 與 MCP manifest 的封裝方式。
