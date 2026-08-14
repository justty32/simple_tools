# AOS（原 Agent Machine）

這個目錄保存 AOS 的設計、實驗與舊 Agent Machine prototype。

> **主線仍是設計提案，尚未實作。** 現行完整設計在 [`full/`](full/README.md)；根層不再放設計文件。
> 2026-08-13 那一代的 23 份根層 Markdown 已整批封存於
> [`archived/2026-08-13-snapshot/`](archived/2026-08-13-snapshot/README.md)，逐 byte 保存，不再是主線。

## 目錄地圖

| 目錄 | 內容 | 地位 |
|---|---|---|
| [`full/`](full/README.md) | AOS 完整設計 `00`–`13` 與 [`options/`](full/options/README.md) 待選方案 | 現行主線設計 |
| [`workbench/`](workbench/README.md) | 每日實驗工作區；[2026-08-14 回合](workbench/2026-08-14/README.md)有 P0／P1 原型與證據 | 暫存，非正式規格 |
| [`wait_user/`](wait_user/README.md) | 給使用者有空再看的想法小卡 | 徵詢中，不阻塞 |
| [`archived/`](archived/README.md) | 被取代但仍有追溯價值的舊文件 | 只供追溯，沒有權威 |
| `agent_machine/` | 舊離線 prototype 的 Python 程式 | 已實作，範圍很窄 |

## 權威順序

1. **產品方向與用詞**：使用者較新的明確決定最高；[`full/00-STATUS-AND-SOURCES.md`](full/00-STATUS-AND-SOURCES.md) 收斂共同邊界。
2. **目前已實作行為**：實際程式與 `python3 -m agent_machine._checks` 為準。
3. **完整設計內部**：`full/` 各主題頁依 00 的狀態標記解讀。
4. **尚未裁決的選擇**：以 [`full/options/README.md`](full/options/README.md) 為準；它們不是承諾。
5. **研究與過程**：`workbench/` 提供實驗證據與決策脈絡，但不是正式 ABI（跨語言固定介面）。
6. **歷史**：`archived/` 只供追溯，不得用來覆蓋現行設計、程式或測試。

## 舊 prototype

以下描述的是既有 Python 程式，不是 `full/` 的設計目標；兩者的 Function、Step 詞義並不相同。

檔案、目錄與 path 直接使用 Linux filesystem，不另外實作記憶體 VFS 或 host filesystem wrapper。
Python 使用 `os`；C++ 使用 `<filesystem>` 與 `<fstream>`。

Windows 開發時一律從 Ubuntu WSL2 執行。Source 可以位於 `/mnt/c`，但 Agent Machine 的 state、workspace
與 filesystem tests 必須位於 WSL home 或 `/tmp` 等 Linux filesystem，不能放在 `/mnt/c`。

最小 Function 是一個目錄，其中的 `run` 是 executable entry：

```text
bot-a/
  run
  llm.json
  messages.json
  tools.json
```

三份 JSON 分別表示指定的 LLM、現有 messages 與可用 tools。新建時只固定頂層形狀：LLM 是 object，
messages 與 tools 是 array。第一個離線版本使用 `{"engine":"echo"}`，`next` 會把最後一則 user
message 原樣回覆；尚未連接真 LLM 或執行 tools。

三份 JSON 都會遞迴解析 `$env` 與 `$ref`：

```json
{
  "engine": {"$env": "BOT_ENGINE", "default": "echo"}
}
```

```json
[
  {"$ref": "messages/system.json"}
]
```

`$ref` 的相對路徑以目前 JSON 所在目錄為準，也可用 `file.json#part/name` 引用片段。字串陣列形式
`{"$ref":["base.json","local.json"]}` 會依序合併 object，後面的值優先；循環引用會直接報錯。

目前從 source 建立：

```sh
python3 -m agent_machine new /tmp/bot-a
```

## Prototype 現有命令

```sh
run_path=$(python3 -m agent_machine start /tmp/bot-a "do something")
python3 -m agent_machine show "$run_path"
python3 -m agent_machine next "$run_path"

python3 -m agent_machine pause "$run_path"
python3 -m agent_machine resume "$run_path"
python3 -m agent_machine stop "$run_path"
```

一次做到底則使用：

```sh
python3 -m agent_machine run /tmp/bot-a "do something"
```

`start` 會在 Bot 內建立 `.agent-machine/runs/run-xxxx/state.json`，並輸出 run path。這個 path 可以交給
另一個 process 的 `next`、`show`、`pause`、`resume` 或 `stop`。`next` 每次最多做一步；對 paused、done
或 stopped 的 run 不會重做。

未來安裝為 Debian package 後，同一入口會是：

```sh
agent-machine new bot-a
```

從 Ubuntu WSL2 執行檢查：

```sh
cd /mnt/c/code/mine/simple_tools/agent-machine
python3 -m agent_machine._checks
```

目前沒有 process lock、crash recovery、LLM/tool side-effect journal 或多步 agent loop。Bot 的來源 JSON
也不會在 run 完成後被自動改寫；本次使用的 messages 保存在該 run 的 `state.json` 中。
