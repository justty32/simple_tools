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

## 為何先用 stdio

MCP 官方 Python SDK 把 `stdio` 定位為由 host 啟動的本機 subprocess transport，把
Streamable HTTP 定位為需要部署的網路 server。第一版的目標正是讓本機 coding agent 控制
同一台機器上的 Python Round，因此 stdio 最合適：

- 不必開 port、做 service discovery 或先建 OAuth；
- MCP server 和 coding-agent session 可以採相同 process lifecycle；
- `Handle`、bot、dispatch callables 與 lock 全留在 Python process；
- stdout 由 MCP protocol 獨占，診斷只寫 stderr。

stdio 並不提供 sandbox 或較低權限。server 與 agentloop tools 仍擁有啟動它的使用者權限。
host 關閉或強殺 child process 時，正在執行的模型請求／工具也會一起被作業系統終止，這和
`Handle.end()` 的合作式安全結束不同。

Streamable HTTP 適合第二階段：獨立 service、重連、多個 hosts 或遠端控制。它同時會立刻
帶來 authentication、authorization、Origin／DNS rebinding、防止跨使用者猜中 `round_id`、
多 client 競爭及部署維護等責任，因此不應只是「把 stdio 改成一個 port」。

舊 HTTP+SSE transport 已被 Streamable HTTP 取代且正式 deprecated，不應做新入口。

## 最新 MCP lifecycle 對本設計的影響

目前官方最新 protocol revision 是 `2026-07-28`。此版移除了 `initialize`／`initialized`
handshake、`Mcp-Session-Id` 與 HTTP GET session stream。每個 request 都攜帶 protocol
version、client capabilities 等 `_meta`，server capability 可由 `server/discover` 查詢。

這表示：

- **MCP connection/session 不是 agentloop Round。** Server 必須由 `agentloop_start` 產生
  明確的 `round_id`，後續每個 tool call 都帶回它。
- `round_id` 是應用層 reference，不是 Python `Handle` 的序列化形式，也不是憑證。
- Streamable HTTP 的每次 request 可能落在不同 worker；若未來多 worker，Round registry
  必須集中或由 round id 做明確 routing。第一版不要多 worker。
- stdio 的單一長壽 process 雖然自然保留記憶體，仍不應省略 `round_id`，否則未來無法平順
  遷移到 HTTP，也容易把 host session、server process 與 Round lifecycle 混成一件事。

官方 Python SDK v2 是目前 stable line，支援 `2026-07-28` 及較早 revisions。實作時應 pin
SDK 的相容版本並使用它的 protocol negotiation／compatibility，不自行寫 JSON-RPC parser。
較舊 clients 仍可能使用 initialize、transport session 與舊式 notification；adapter 自己的
Round API 仍維持明確 `round_id`，不隨協議版本改變。

## 第一版 tools

應使用分開的 tools，而不是一個接受任意 `action` 的萬用 tool。分開後模型較容易選對工具，
host 也較可能針對 `edit`／`end` 等高風險能力做獨立批准。所有結果都回傳
`structuredContent`，另附短文字摘要供只展示文字的 client 使用。

| MCP tool | agentloop 行為 | 建議結果 |
|---|---|---|
| `agentloop_start(profile, prompt, ...)` | 由啟動時註冊的具名 factory 建 bot／dispatch／Handle，再 `threading.start()` | `round_id`、初始 state、revision |
| `agentloop_status(round_id, detail)` | 在 Handle lock 下複製 snapshot | state、step、calls、token、stop、event sequence、按 detail 截斷的資料 |
| `agentloop_wait(round_id, after_seq, states, timeout_ms)` | 等待下一個 boundary/state event；不持有 Handle lock 長期等待 | events、最新 snapshot、`timed_out` |
| `agentloop_pause(round_id)` | `handle.pause()` | accepted、當下 state；只是提出要求 |
| `agentloop_resume(round_id)` | `handle.resume()` | accepted、當下 state |
| `agentloop_end(round_id, reason)` | `handle.end(reason=...)` | accepted、當下 state；running 時尚未表示已完成 |
| `agentloop_edit(round_id, expected_revision, operations)` | `with handle.edit():` 內驗證並原子套用 | 新 revision 與修改後摘要 |

`start` 不接受 pickle、任意 Python expression、import path 或任意 callable。可用哪些 bot、
dispatch 與預設 Limits，應由啟動 server 時設定的具名 profile/factory 決定。第一版可以只允許
一個 active Round，但 wire format 仍保留 `round_id`。

### status

Snapshot 建議包括：

```text
round_id, revision, state, step, message, text, finish_reason,
tool_calls, tool_results, tool_log 摘要,
calls, used, input_tokens, output_tokens, cached_input_tokens,
auto_finish, stop, end_reason, err 的安全文字摘要,
latest_event_seq
```

預設結果必須有長度上限。完整 message、tool result 與 log 以 `detail`、分頁或日後 Resource
另外取得；不可把 exception object、bot、history object、dispatch callables、lock 或任意
不可序列化資料搬出 process。

### wait 與 cancellation

`agentloop_wait` 是 bounded long-poll：client 傳入上次收到的 `after_seq`，server 返回較新的
事件，或在 timeout 後返回 `timed_out=true`。Server 應限制最大 timeout，避免 coding agent
的一次 tool call 永久卡住。client 斷線或取消 `wait`，只取消這次等待，**不等於**
`pause()`、`end()` 或中斷 agentloop 正在執行的模型／工具。

事件 queue 應有固定上限；若 client 落後到事件已被淘汰，回傳 `gap=true` 並要求重新讀
`status`。`seq` 只在單一 Round 內單調遞增，不拿 wall-clock timestamp 當順序。

### pause、resume 與 end

MCP response 必須保留 agentloop 的合作式語意：

- `pause` 在 `running_step`／`running_tools` 時只表示要求已接受；真正停住要再 `wait` 到
  `paused`，或讀取 `status`。
- `resume` 只喚醒 `waiting`／`paused`，或取消尚未生效的 pause；修改資料本身不自動 resume。
- `end` 在 parked 時可立即完成；running 時要等現有 Step+callbacks 或 tool batch+callbacks
  的安全邊界。`accepted=true` 不等於 `state=completed`。
- `safe=False` 不應出現在 MCP schema；adapter 不提供核心本來就不支援的 unsafe 操作。

### edit

Python 同 process API 刻意允許操作者直接修改 Handle；跨 process MCP 不可能保留「任意
Python object」的自由度。第一版只接受明確的 JSON operations 與欄位 allowlist，例如：

```text
prompt, images, ask_options, tool_calls, tool_results, auto_finish
```

不要接受任意 attribute path，尤其不可改 `_lock`、`_condition`、`_runner`、內部 request
flags、bot 或 dispatch callable。若要變更 dispatch，只能切換 server 事先註冊的 profile／
tool binding，不能從 MCP payload 注入 code。

`expected_revision` 用來避免兩個 controllers 在不知情下互相覆蓋；不符合時回 conflict 與
最新 revision。若呼叫者明確接受 last-writer-wins，可省略它。單次 operations 必須先完整
驗證，再在一次 `handle.edit()` 中原子套用；失敗不可留下半套修改。

## Boundary events 與 callback gate

在 Python runner process 內註冊兩個很薄的 callbacks：

```text
after_step  → immutable snapshot → bounded event queue → CONTINUE／PAUSE
after_tools → immutable snapshot → bounded event queue → CONTINUE／PAUSE
```

事件至少帶：

```text
round_id, seq, boundary (after_step | after_tools), step,
state, revision, tool_calls/tool_results 摘要, gate
```

Callback 在 runner thread 且持有 Handle 的 `RLock`；它只應複製有限 snapshot、enqueue，然後
立刻返回。不可從 callback 等 MCP client 回應，也不可建立會操作同一 Handle 的 child thread
後 `join()`。

兩種模式沿用 Pi 文件的定義：

- **observe**：callback 回 `CONTINUE`。事件是事後觀察，client 收到前 runner 可能已進到下個
  operation，不能保證修改趕得上。
- **gate**：callback 回 `PAUSE`。需要在工具執行前審核／改寫 `tool_calls` 時，設定
  `after_step` gate；需要在下一個 Step 前改寫 `tool_results` 時，設定 `after_tools` gate。

第一版建議 `after_step` gate 可配置且預設開啟，`after_tools` 預設 observe；profile 可以改變
預設值。這能讓 coding agent 在最危險的「模型提出工具 → 真正執行」邊界可靠介入，而不要求
所有 Round 每一步都人工恢復。

`wait` 是 boundary event 的可靠交付方式。不要把任意 boundary event 假裝成 MCP
`notifications/tools/list_changed`；工具清單根本沒有變。

## Tools、Resources、Prompts、notifications 如何分工

### Tools：第一版唯一必要 primitive

MCP tools 是 model-controlled、具 JSON Schema 的 action，正好承載 status、wait 與控制桿。
官方規格也提醒 clients 對工具執行提供清楚顯示與人工拒絕能力，但實際 UI 與批准行為由 host
決定。因此 server 仍要自己驗證輸入、權限與 Round 狀態，不能把安全性寄託在描述或 tool
annotations。

### Resources：適合快照與 log，不適合當唯一控制面

日後可選擇暴露：

```text
agentloop://round/{round_id}/snapshot
agentloop://round/{round_id}/events
```

Resources 是 application-controlled；不同 host 可能只讓人手動加入 context，可能自動讀，
也可能根本不把它交給模型。因此第一版仍以 `agentloop_status`／`agentloop_wait` 為準。
Resource 適合大型 read-only detail，control mutation 仍必須是 tool。

最新版 MCP 可以讓 client 透過 `subscriptions/listen` 訂閱特定 resource URI，server 再送
`notifications/resources/updated`。這可作低延遲提示：「snapshot 變了，請重讀」，但
notification 本身不取代 snapshot/event queue，也不保證所有 coding-agent clients 都實作或
向模型暴露訂閱。

不要每個 boundary 發 `resources/list_changed`；Round snapshot 內容變更不代表 resource
清單變更。

### Prompts：只是 UX 範本

Prompts 是 user-controlled 的 reusable templates，適合日後提供「檢查下一批 tool calls」、
「摘要 Round」或「安全收尾」等操作說明。它不能 pause runner，也不能保證 host 執行其中
建議；不是控制協議。第一版不需要 prompt primitive，各 coding agent adapter 可先用自己的
slash command／instruction 做 UI。

### Notifications：只作提示，不作控制確認

最新版的 `subscriptions/listen` 把 list/resource change notifications 集中在一條由 client
主動建立的 stream。Client capability 與 host UX 會不同，而且舊 protocol revisions 使用的
notification/session 形狀不同。第一版用 tool result 加 `wait(after_seq)` 保證可攜性；支援
notifications 時，也必須能在遺漏、重連或不訂閱的情況下靠 status+sequence 恢復。

## Sampling 與 elicitation

### Sampling：不要採用

Sampling 讓 MCP server 請 client host 的模型產生內容，但在 `2026-07-28` 已正式 deprecated，
官方建議新實作直接整合 LLM provider API。Agentloop 本身已持有 bot／模型，若再透過 sampling
呼叫外層 coding agent，還會混淆兩個模型迴圈、權限、token accounting 與取消語意。因此
本 adapter 不宣告也不依賴 sampling。

### Elicitation：保留為日後可選批准 UI

Elicitation 仍是現行能力。最新版透過 Multi Round-Trip Requests：server 的 tool result 回
`resultType="input_required"` 與 input requests，client 收集輸入後用新的 JSON-RPC id 重試原
request。Form mode 不可要求 password、API key、access token 或付款憑證；敏感互動必須使用
URL mode。

它可以日後用於「這次是否真的 end／resume／套用 edit」的人類確認，但第一版不應依賴：

- client 必須明確宣告對應 elicitation capability／mode；各 coding agent 支援程度不同；
- control tool 本身已是短 request，host 更適合在呼叫工具前做批准；
- elicitation 不是 boundary event transport，也不應拿一個 pending request 長期等 Round；
- MRTR 需要保存與驗證 `requestState`、處理 accept／decline／cancel 及重試冪等性。

因此第一版遇到 client 不支援 elicitation 時不降級成「自動同意」。要求人工批准的 profile
應以 `after_step` gate 暫停，直到另一個明確的 `edit`／`resume`／`end` tool call。

## Client capability 差異

「支援 MCP」不代表 host 會完整提供 tools、resources、prompts、subscriptions、elicitation
或一致的人工批准 UI。Adapter 應：

1. 把 **tools/call + polling wait** 當最低共同能力；
2. 依 request `_meta` 的 client capabilities 決定是否提供 MRTR elicitation 等增強流程；
3. 沒有 resource subscription 時仍能用 sequence + status 恢復；
4. 不假設 prompt 一定顯示成 slash command、resource 一定自動放進模型 context；
5. 對不支援必要 tool calling 的 host 明確報錯，而不是另開隱藏控制通道。

正式實作前，仍需由 Pi、Claude Code、Codex 各自的 adapter 調查文件確認：它們支援哪些 MCP
transport／protocol revision、是否向模型暴露 structured results、是否能批准 mutating tools、
是否支援 resources/subscriptions/elicitation。MCP server 的核心 tool schema 不因 host 改變，
host-specific layer 只補呈現與 lifecycle。

## 安全與權限

- `round_id` 只用來定位 Round，不是 authorization token。Streamable HTTP 必須把每次 request
  的認證 principal 與 Round owner／ACL 綁定。
- `status` 也可能洩漏 prompt、路徑、原始 tool arguments/results 與 secrets；預設要截斷、
  redact，詳細資料需較高權限。
- `edit`、`resume`、`end` 與 `start` 是 mutating operations，應能和 read-only
  `status`／`wait` 分開授權與批准。
- tool annotations 只能是 UI hint；官方規格要求 client 把來自不可信 server 的 annotations
  視為不可信，server 端不可用它代替 enforcement。
- 不允許任意 code/import/pickle、私有欄位、callable 或 exception object 穿過 JSON 邊界。
- 多 controller 修改要使用 revision conflict；同一 Handle 仍只有一條 runner thread。
- event queue、result size、wait timeout、active Round 數量與 completed retention 都要有上限，
  防止記憶體與 thread 耗盡。
- 若使用 Streamable HTTP，應採官方 authorization guidance，驗證 Origin／Host 並防 DNS
  rebinding；不要因只 bind localhost 就假設瀏覽器無法攻擊。
- 這個 MCP server 不是 sandbox。Agentloop dispatch 的檔案、shell、網路等權限仍需由外部
  process/container/policy 決定。

## Round 與 server lifecycle

第一版建議每個 registry entry 保存：

```text
round_id, Handle, BackgroundRun, event queue, revision,
created_at, last_accessed_at, owner（HTTP 才需要）
```

- `completed`／`error` 後保留有限時間供 status 讀取，再明確 evict。
- MCP server 正常 shutdown 時，對 active Rounds 呼叫 `end(reason="server_shutdown")` 並嘗試
  `join`。因為 cooperative end 無法切斷已開始的模型／工具，shutdown 可能無法在固定時間內
  完成；部署層最後強殺 process 時要承認這是 unsafe termination。
- stdio EOF 代表 host 已離開，不代表 Round 自然完成。第一版採同 lifecycle：best-effort
  end，再退出；不承諾 reconnect。
- Streamable HTTP server restart 會失去 process-local Handle。要支援 reconnect／持久化，需在
  agentloop 上層新增可恢復 runtime，不能只保存 JSON snapshot 後假裝 Python runner 還活著。
- 一個 Pi／Claude Code／Codex session 可控制多個 Round；反過來，host 的 fork/resume/new
  不會 fork、轉移或重用 Handle。

## 與各種入口的關係

```text
Python REPL
  └─ 同 process 直接持有 Handle：自由度最高

通用 MCP server
  └─ 跨 process、JSON allowlist、可被多種 MCP host 共用

Pi／Claude Code／Codex 專用 adapter
  └─ 呼叫相同 MCP tools，補各產品的 UI、command、approval、啟停與設定
```

Python REPL 繼續是「全部公開狀態、操作者承擔風險」的基準入口。MCP 是遠端 controller API，
必須承認 serialization、authorization 與 concurrency 邊界，因此不應聲稱完整等價於直接
持有 Handle。

對 Pi 而言，若其 MCP client 能滿足最小工具需求，先前規劃的 TypeScript extension 可以縮成
UX layer，或完全由 MCP 設定取代；若需要 Pi 特有 slash commands／TUI gate，extension 仍可
呼叫 MCP client，而 Python 端只保留一份 server。Claude Code 與 Codex 也遵循同一原則：先用
共同 MCP tools，只有官方 MCP host 缺少必要 lifecycle/UI 時才寫薄的產品 adapter。

## 建議實作順序

1. 用官方 Python SDK v2 建 stdio server，單 process、單 active Round、明確 `round_id`。
2. 實作 start/status/wait/pause/resume/end；snapshot 截斷、event seq 與 bounded queue。
3. 加 after-step／after-tools observe/gate callbacks，驗證安全邊界。
4. 實作 allowlisted atomic edit 與 optional `expected_revision`。
5. 用 MCP Inspector 加至少一個真實 coding-agent host 做端到端測試。
6. 根據各 host 實測，再決定 resources/subscriptions、elicitation 或專用 UX adapter。
7. 只有出現跨 session／多 client／遠端需求後，才做 Streamable HTTP、認證與 Round service。

## 官方來源

以下來源最後核對於 2026-08-11：

- [MCP 2026-07-28 specification](https://modelcontextprotocol.io/specification/2026-07-28)
- [2026-07-28 版本變更說明](https://blog.modelcontextprotocol.io/posts/2026-07-28/)
- [Streamable HTTP transport](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http)
- [stdio transport](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio)
- [Tools](https://modelcontextprotocol.io/specification/2026-07-28/server/tools)
- [Resources 與 subscriptions](https://modelcontextprotocol.io/specification/2026-07-28/server/resources)
- [Prompts](https://modelcontextprotocol.io/specification/2026-07-28/server/prompts)
- [Elicitation](https://modelcontextprotocol.io/specification/2026-07-28/client/elicitation)
- [Sampling（已 deprecated）](https://modelcontextprotocol.io/specification/2026-07-28/client/sampling)
- [官方 Python SDK](https://github.com/modelcontextprotocol/python-sdk)
- [官方 Python SDK：transport 選擇](https://py.sdk.modelcontextprotocol.io/run/)
