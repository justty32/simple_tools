# Agent Machine

Agent Machine 的獨立、Linux-only 實作。Python 先固定行為，之後再以 C++／Janet 實作同一份契約。

> **目前程式只是第一個離線 prototype。** 它仍使用 `show`、`resume` 與 Run path，不是最新操作介面。
> 先看給使用者把關的 [`DESIGN.md`](DESIGN.md)；完整方向見 [`INTERFACE.md`](INTERFACE.md)，精確指令行為見 [`COMMANDS.md`](COMMANDS.md)，
> 使用情境見 [`SCENARIOS.md`](SCENARIOS.md)，exec tool 格式見 [`EXEC.md`](EXEC.md)。在下一輪實作前，
> 不把下方舊命令當成正式契約。

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
