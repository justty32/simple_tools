# Claude Code 的 MCP 介面

[← 返回 Claude Code adapter 總覽](README.md)

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
