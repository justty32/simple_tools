# agentloop 互動入口

這個目錄存放讓人或其他 coding agent 互動操作 agentloop 的入口資料，包含：

- 各種互動環境的使用文件；
- 把其他 coding agent 當作進一層操作入口的方式；
- 啟動、連接或預載 agentloop 的薄腳本；
- 可複製或直接執行的範例。

目前已有 Python 原生 REPL 用法，以及 Pi coding agent 的第一版 extension／bridge。
這些入口都只組合 agentloop 公開 API，不為核心增加隱藏控制通道。

這些入口的共同目的，是讓操作者直接持有 `Handle`，自由查看或修改狀態，而不需要先把每種
操作包成 CLI command。

目前程式與文件已依用途分為：

```text
freepy/adapters/             # 可執行 control adapters
docs/freepy/interfaces/      # 直接互動介面
docs/freepy/adapters/        # host／protocol 文件
```

## 已記錄的入口

- [Pi coding agent](../adapters/pi/README.md)：比較 Pi extension、Python bridge、RPC 與
  launcher，並記錄已實作的第一版。
- [Claude Code](../adapters/claude-code/README.md)：推薦使用 project-local stdio MCP server 與薄 skill。
- [OpenAI Codex](../adapters/codex/README.md)：推薦使用 project-scoped stdio MCP server 與薄 skill。
- [MCP](../adapters/mcp/README.md)：共用 Python MCP server 的協議、tools、events 與 lifecycle 設計。

Pi 離線範例與啟動方式見 [Pi quickstart](../adapters/pi/quickstart.md)。

## Python 原生 REPL

## 啟動 Python

從 repository 的 `freepy` 目錄執行：

```bash
PYTHONPATH=llmkit uv run python
```

或直接使用預載常用名稱的 launcher：

```bash
python -m shells repl
```

進入後已有 `LLM`、`Engine`、`Params`、`Controller`、`Handle`、`session`，以及 `llms`、
`base_tools`、`agentloop` modules。

## 準備 bot 與工具

REPL 不會自行建立 `bot` 或 `dispatch`。呼叫者需要先依自己的應用程式準備：

```python
bot = ...

dispatch = {
    "read_file": read_file,
    "write_file": write_file,
}
```

`dispatch` 是「模型要求的工具名稱」到「真正執行的 Python callable」的 mapping。

## 啟動一個可互動的 Round

agentloop 核心的 `run()` 是同步函式。REPL 用 `session()` 啟動背景 runner；它預設建立
`auto_finish=False` 的 Handle，讓自然回答停在 `waiting`：

```python
c = session(bot, dispatch, "先檢查專案")
h = c.handle
```

`auto_finish=False` 表示模型不再要求工具時，Round 進入 `waiting`，不會直接完成。
因此操作者仍可追加內容並繼續同一個 Round。

## 查看狀態

```python
h.now()
h.state
h.step
h.message
h.tool_calls
h.tool_results
h.tool_log
h.input_tokens
h.output_tokens
h.cached_input_tokens
h.err
```

常見狀態包括：

- `ready`：上一個 operation 已提交、下一個尚未開始；逐步 runner 的安全邊界。
- `running_step`：模型正在產生下一則 message。
- `running_tools`：正在執行一批工具。
- `waiting`：模型自然靜止，等待外部繼續。
- `paused`：controller 主動暫停。
- `completed`：Round 已正常結束。
- `error`：Round 因錯誤結束。

## 暫停、修改與繼續

```python
h.pause()
h.wait_until_paused()

with h.edit():
    h.prompt = "改成檢查另一個目標"
    h.tool_calls.clear()

h.resume()
```

`pause()` 是合作式操作，不會切斷正在執行的模型請求或工具批次：

- 已在 `ready`／parked：立即成為 `paused`。
- Step 中要求暫停：完成 Step 與 `after_step` callbacks，停在 tools 前。
- tools 中要求暫停：完成整批 tools 與 `after_tools` callbacks，停在下一個 Step 前。

`pause()` 立即返回；需要確認已經真正停住時，使用 `wait_until_paused()`。

## 在 waiting 時追加輸入

```python
h.wait_for_state("waiting")

c.send("再檢查 Windows 的情況")
```

修改公開資料本身不會喚醒 runner。只有明確呼叫 `resume()` 或 `c.send()` 才會繼續。

如果希望這次回答後自然完成，可以在恢復前設定：

```python
c.send("整理最後結論", finish=True)
```

## 安全結束

不打算再繼續這個 Round 時：

```python
c.end(reason="operator")
result = c.join()
```

`end()` 與 `pause()` 使用相同的安全邊界：

- 已在 `ready`／`waiting`／`paused`：立即完成，並在需要時喚醒 parked runner。
- 正在執行 Step：完成 Step 與 callbacks，在 tools 前結束。
- 正在執行 tool batch：完成整批與 callbacks，再結束。

`c.join()` 等待背景 runner 返回，並取得同一個 Handle。若 Round 還在
`waiting`／`paused` 且沒有先 `resume()` 或 `end()`，`join()` 會繼續等待。

## 直接修改公開狀態

Handle 刻意暴露資料，REPL 可以直接增刪查改：

```python
with h.edit():
    h.dispatch["new_tool"] = new_tool
    h.ask_options["tool_choice"] = "required"
    h.tool_results["call_1"] = "人工改寫的結果"
```

外部操作者承擔修改錯誤、資料不一致與安全 policy 的風險。跨 thread 一次修改多個欄位
或 nested list/dict 時，使用 `with h.edit()`；單純讀取或能接受競態風險時，可以直接操作。

## Callback

可以在啟動 Round 前註冊 callback：

```python
def inspect_calls(handle):
    print(handle.tool_calls)
    return agentloop.CONTINUE

def inspect_results(handle):
    print(handle.tool_results)
    return agentloop.CONTINUE

h = agentloop.Handle(auto_finish=False)
h.after_step.append(inspect_calls)
h.after_tools.append(inspect_results)
c = session(bot, dispatch, "開始", handle=h)
```

callbacks 在 runner thread 同步執行，而且持有 Handle 的 `RLock`。不要在 callback 裡
建立另一條需要操作同一個 Handle 的 thread，然後立刻 `join()` 它，否則可能互相等待。

## 一段完整流程

```python
c = session(bot, dispatch, "分析目前專案")
h = c.handle

h.wait_for_state("waiting", "paused", "completed", "error")
h.now()
h.message
h.tool_log

if h.state in {"waiting", "paused"}:
    c.send("再檢查測試是否完整", finish=True)

result = c.join()
result.now()
```

## 能力邊界

- 這不是 sandbox；REPL 與 tools 擁有目前 Python process 的權限。
- Handle 只供同一個 process 內的 threads 共用，不支援跨 process 傳遞。
- 一個 Handle 只能執行一個 Round，而且只能有一條 runner thread。
- paused／waiting Round 會持續占用原 runner thread。
- REPL 不提供 scheduler、thread pool、持久化或多 agent 管理。

核心控制語意見 [Round 與 Step](../../../freepy/agentloop/ROUNDS.md)，背景 thread 細節見
[`agentloop.threading`](../../../freepy/agentloop/threading/README.md)。
