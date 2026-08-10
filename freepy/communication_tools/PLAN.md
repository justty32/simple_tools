# communication_tools 規劃

**這份是實作規格，尚未寫程式。** 這層只負責 agent 之間可靠傳訊；不知道團隊、上下級、任務、資源或權限是什麼。

## 一句話

```text
communication_tools = 有身分的 mailbox transport
team_tools          = 建在它上面的組織與工作流
```

第一版用同一個檔案系統，不設 server、daemon 或 port。未來可換 SQLite、Redis 或遠端 broker，但模型看到的工具介面不因儲存實作改變。

## 身分與位址

身分由 supervisor 在啟動時綁定，不能做成模型可呼叫的工具：

```python
communication_tools.bind(
    identity="/root/planner/worker-3",
    root="/run/freepy/comm/session-x",
)
```

identity 是 canonical agent path。每段只允許 `[A-Za-z0-9._-]`、最多 64 字；必須是從 `/root` 開始的絕對路徑，不接受空段、`.`、`..`、保留段 `.agent`、尾端 `/` 或重複 `/`。它是邏輯 namespace，不是宿主絕對路徑；落盤時只能用 `agent_identity.py` 驗過的 segments 逐段 join。`bind()` 另行解析並固定 storage root；每個操作都再次檢查路徑沒有越界或穿過 symlink。

endpoint 由 supervisor provision，agent 不能自己註冊成別人：

```text
<root>/endpoints/root/planner/worker-3.json
<root>/mailboxes/root/planner/worker-3/new/<message>.json
<root>/mailboxes/root/planner/worker-3/read/<message>.json
```

endpoint 可帶允許寄件／收件對象的 transport policy。它只回答「能不能傳」，不表達主管或角色；組織語意由 `team_tools` 判斷。

## 三個模型工具

| 工具 | 參數 | 行為 |
|---|---|---|
| `check_messages` | 無 | 只看 `new/` 檔名，回數量、寄件者與時間 |
| `read_messages` | `limit=10` | 讀最舊的信，再移進 `read/` |
| `send_message` | `to`, `text` | 寄給一個 canonical agent path |

不提供 `join_team`、`role` 或全域 `to="*"`。團隊廣播必須由 `team_tools` 依當下成員與授權展開成多筆直接訊息，避免 transport 暗中擁有一份組織名單。

`check` 和 `read` 分開是刻意的：前者只需一次 `listdir`，不把正文灌進 context；後者才支付內容成本。

## 訊息格式

```json
{
  "id": "01K...",
  "from": "/root/planner",
  "to": "/root/planner/worker-3",
  "at": "2026-08-10T12:34:56Z",
  "text": "請檢查 parser；背景見 [規格](memory:t7)"
}
```

正文仍是純文字，不在 transport 發明 task RPC。需要分享大型內容時直接放 `memory:` Markdown link；是否能載入由 `memory_tools` 檢查，拿到 ref 本身不等於取得權限。

v1 的 `to` 只接受 canonical absolute path，先不做 `../peer` 相對位址。相對位址雖像 Unix，但會讓 reparent、稽核紀錄與舊訊息重播產生歧義；UI 可以顯示短名，落盤仍寫完整 path。

## 原子性與讀取語意

投信順序：

1. 在收件者 `new/` 同一檔案系統建立隨機暫存檔。
2. 完整寫入、flush、關閉。
3. 用 `os.replace()` 原子改名成正式檔名。

讀信先挑最舊的正式檔，成功 parse 後以 `os.replace()` 搬到 `read/`。第一版採 **at-least-once observation**：若讀取結果回給模型前 process crash，信可能已在 `read/`，但不會出現半封 JSON；歷史仍可稽核。之後若需要重送，再加 claim/ack 目錄，不在 v1 假裝 exactly-once。

檔名包含時間、寄件者與隨機／sortable id，讓 `check_messages` 不開正文也能摘要。JSON 內的 `from` 必須等於綁定 identity，不能信任呼叫參數。

## 不做阻塞式等信

沒有 `wait_for_message`。長時間阻塞會讓外部無法區分等待和卡死，也會污染 `agentloop` 的時間預算。

supervisor 可在 mailbox 有新信時，把內容轉成回合內追加指令；這是 agent control plane，
不是 communication tool 自己阻塞。工具內部需要的互動由工具自己處理，不進 agentloop。

## 不變條件

- 模型不能修改自己的 identity、root 或 transport policy。
- canonical path 必須按 segment 解析；權限判斷不能用裸字串 `startswith()`。
- 寄件者由綁定狀態填入，不接受模型偽造。
- 不存在的 endpoint 與未授權收件者都回明確錯誤。
- 寫入只有「看不見」或「完整可見」兩種狀態。
- `read_messages(limit)` 有筆數與單封／總位元組上限。
- 所有錯誤回字串，與其他 tool dispatch 一致。

## 實作順序與離線關卡

1. 共用 `agent_identity.py`：canonical path 與安全 segments。
2. `paths.py`：storage root、endpoint 路徑與 symlink 檢查。
3. `identity.py`：只由宿主呼叫的 bind/provision。
4. `mailbox.py`：send/check/read 與原子改名。
5. `__init__.py`：`tools()` 產生 schema + dispatch。
6. `__main__.py`：臨時目錄離線測試。

必測：同時寄 100 封不遺失、看不到半封、不能偽造 sender、不能越界、未授權 recipient 被擋、check 不讀正文、讀後留在 `read/`、壞 JSON 隔離且不阻塞其他信。
