# MCP 事件與 capabilities

[← 返回 MCP adapter 總覽](README.md)

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
