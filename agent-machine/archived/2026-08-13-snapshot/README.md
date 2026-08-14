> **2026-08-13／14 歷史 snapshot，來源 commit `32882d4`，沒有權威；現行主線仍在 [`agent-machine/` 根層](../../README.md)，使用者後續決定高於本頁。**
> 本世代分成 AOS 草稿、AgentOS prototype、思考／審查；原檔與 OPUS 七頁切分見 [MANIFEST](MANIFEST.md)。
> 舊稿的 Function＝process、Task＝舊式 Steps 等語意已被覆蓋；資料夾、Step、Round 都可為 Function，執行中的 Function 實例才是 Task，詳見[使用者決定](../../workbench/2026-08-14/idea-ledger/USER-DECISIONS.md)。

# AOS（原 Agent Machine）

這個目錄保存 AOS 的設計與舊 Agent Machine prototype。

> **目前主線仍是提案，尚未實作。** 先看 [`AOS-ARCHITECTURE.md`](AOS-ARCHITECTURE.md) 的 Task／Step
> 模型與 Linux 邊界、[`AOS-SCHEDULING.md`](AOS-SCHEDULING.md) 的 queue／executor 排程，再看
> [`AOS-V0.md`](AOS-V0.md) 的第一版功能。AOS 完成後，agent loop 如何建立在其上
> 見 [`AGENTLOOP-ON-AOS.md`](AGENTLOOP-ON-AOS.md)；工作目錄與狀態目錄的放法見
> [`AOS-INTEGRATION.md`](AOS-INTEGRATION.md)。
>
> 現有 Python 程式仍是第一個離線 prototype，使用 `agent-machine`、Run path、`show` 與 `resume`。
> `DESIGN.md`、`INTERFACE.md`、`COMMANDS.md` 等文件描述更早的 AgentOS 介面，只保留作為研究材料，
> 不再決定下一輪實作。

## 舊 prototype

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
