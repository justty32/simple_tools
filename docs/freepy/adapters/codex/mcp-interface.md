# Codex adapter 的 MCP 介面

[← 返回 Codex adapter 總覽](README.md)

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
