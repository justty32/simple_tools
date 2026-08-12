# Python REPL：直接操作 agentloop

這個入口適合人在終端機裡觀察、暫停、修改並繼續一個 Round。launcher 會預載常用 API，
但不會替你選模型、探測 endpoint、授權工具或建立 sandbox；模型服務與工具範圍都由操作者明確指定。

## 從零啟動

在 repository 根目錄用 PowerShell：

```powershell
$env:PYTHONPATH = "freepy"
python -m shells repl
```

已經位於 `freepy/` 目錄時可直接執行：

```powershell
python -m shells repl
```

Bash／WSL 從 repository 根目錄則是：

```bash
PYTHONPATH=freepy python -m shells repl
```

launcher 優先使用 `freepy/.venv` 的 Python，找不到才使用目前的 Python。成功後會看到：

```text
已就緒: LLM, Engine, Params, Controller, Handle, Assistant, assistant, toolbox, session; ...
工具 workspace: C:\code\mine\simple_tools
```

這些名稱與 `llms`、`base_tools`、`agentloop` modules 都已載入，不必再 import。

### cwd 與工具 workspace

REPL 保留啟動 launcher 時的 cwd；`base_tools` 也以該位置作為預設 workspace。先確認再給模型工具：

```python
>>> import os
>>> os.getcwd()
'C:\\code\\mine\\simple_tools'
>>> base_tools.get_root()
WindowsPath('C:/code/mine/simple_tools')
```

要縮小或切換工具可碰的範圍，明確設定一次：

```python
>>> base_tools.set_root("freepy")
WindowsPath('C:/code/mine/simple_tools/freepy')
```

`base_tools` 只接受 workspace 內的路徑，但這不是 OS sandbox；REPL 與工具仍擁有目前 Python
process 的權限。把 `run_shell` 交給模型前，應先確認 workspace 與自己的核准政策。

## 選擇目前可用的模型

endpoint 不是常駐依賴。公司與家中的範例只是已知設定；每次使用前仍要確認服務真的在線，
也可以把相同寫法換成當下可用的 model、URL 與 timeout。

### 家中：本機 proxy preset

家中可跑的模型是 `lm-gemma-4-12b`。這個 preset 指向 `http://localhost:4000` 的本機
LiteLLM proxy，所以要先啟動 proxy 與模型服務：

```python
>>> setup = assistant(
...     "lm-gemma-4-12b",
...     base_tools.tools(),
...     system="先查證再回答；需要修改前先說明。",
... )
```

這個 id 不是遠端 fallback；`localhost:4000` 沒有服務時仍會失敗。

### 公司：直連 Ollama

公司網路目前可直接使用 Ollama 的 OpenAI-compatible API。不要在家中或離線測試假定這個
LAN 位址存在：

```python
>>> engine = Engine(
...     model="qwen2.5:14b-instruct-q4_K_M",
...     url="http://192.168.1.146:11434/v1",
...     key="ollama",
...     timeout=120,
...     params=Params(temperature=0, max_tokens=4096),
... )
>>> setup = assistant(
...     engine,
...     base_tools.tools(),
...     system="先查證再回答；需要修改前先說明。",
... )
```

模型名稱必須和當下 `GET http://192.168.1.146:11434/api/tags` 的結果一致。若改走本機 proxy，
則改用它提供的 preset id；不要把 Ollama 原生名稱與 proxy alias 混用。

## `Assistant` 與工具配對

`assistant()` 回傳 `Assistant(bot, dispatch)`，把送給模型的 tool schemas 和真正執行 effect 的
Python callables 配在一起：

```python
>>> setup = assistant("lm-gemma-4-12b", base_tools.tools())
>>> bot, dispatch = setup                 # 仍可像 tuple 一樣解包
>>> setup.bot is bot
True
```

第一個參數可以是 preset id、自己建立的 `Engine`，或 `None`。後續每個 tool source 可以是
單一 callable，或 `base_tools.tools()`、`exec_tools.tools(...)`、`tooljson.tools(...)` 這類
`(schemas, dispatch)` bundle。需要先合併時使用 `toolbox()`：

```python
>>> schemas, dispatch = toolbox(one_callable, base_tools.tools())
```

重複工具名稱以及 schema／dispatch 名稱不一致會立即報錯，避免同名 effect 被靜默取代。
helper 不會自行掃描工具、選擇 workspace 或提供 permission policy。

## 啟動互動 Round

最短寫法是從配好的 `Assistant` 直接開 session：

```python
>>> c = setup.session("先讀 README，說明這個專案目前能做什麼")
>>> h = c.handle
```

等價的完整寫法是：

```python
>>> bot, dispatch = setup
>>> c = session(bot, dispatch, "先讀 README，說明這個專案目前能做什麼")
>>> h = c.handle
```

`session()` 建立一條背景 runner thread，新的 Handle 預設 `auto_finish=False`。模型自然回答完且
不再要求工具時會停在 `waiting`，讓你檢查並追加輸入，而不是直接結束 Round。

## 等待與查看狀態

```python
>>> c.wait()                         # 等到可介入或已終止
True
>>> print(c.now())
第 2 步，等待繼續，1 個工具，... tokens，...
>>> h.state
'waiting'
```

無參數的 `c.wait()` 等待 `waiting`、`paused`、`completed` 或 `error`；即使 Round 很快結束，
也不會因只等 `waiting` 而卡住。要等明確狀態或限制時間：

```python
>>> c.wait("waiting", timeout=10)          # 抵達回 True，逾時回 False
True
>>> c.wait("paused", "completed", timeout=2)
False
```

指定 states 時完全保留 `Handle.wait_for_state()` 的 bool 語意。`timeout` 不會取消正在進行的
模型請求或工具，只代表這次等待停止。

常見狀態：

- `idle`：Controller 尚未啟動。
- `ready`：上一個 operation 已提交、下一個尚未開始，是安全邊界。
- `running_step`：模型正在產生下一則 message。
- `running_tools`：正在執行一批工具。
- `waiting`：模型自然靜止，等待外部輸入。
- `paused`：操作者要求暫停後抵達安全邊界。
- `completed`：Round 已正常且永久結束。
- `error`：endpoint、callback 或 runner operation 失敗。

### 回答、記憶與工具紀錄

```python
>>> h.message             # 最新 Step 的純文字；h.text 是同值方便欄位
>>> h.reply               # 最新的 llms.Reply
>>> h.history             # bot 的持續對話記憶，同一個 live list
>>> h.tool_calls           # 最新 message 提出的 tool calls
>>> h.tool_results         # 目前邊界尚待送回模型的 call-id -> result
>>> h.tool_log             # 累積 (step, tool name, args, output)
>>> h.calls, h.used        # 累積工具次數，以及依名稱統計
>>> h.input_tokens, h.output_tokens, h.cached_input_tokens
>>> h.finish_reason, h.stop, h.err
```

`message`／`tool_calls`／`tool_results` 是目前邊界的 live state，之後的 operation 可能覆寫或清空；
需要回顧整輪工具 effect 看累積的 `tool_log`，需要回顧對話看 `history`。`h.now()` 是給人快速看的
摘要，不取代這些欄位。

## 追加、暫停、恢復與結束

Round 在 `waiting` 或 `paused` 時，可以送下一筆 prompt 並恢復：

```python
>>> c.send("再檢查 Windows 的測試", finish=False)
>>> c.wait()
True
```

`send()` 不是任意時刻可寫入的 queue；在其他 state 呼叫會拋出 `RuntimeError`。`finish=True`
會把 `auto_finish` 改成 `True`，讓下一次自然回答後完成：

```python
>>> c.send("整理最終結論", finish=True)
>>> result = c.join(timeout=120)
>>> result.state
'completed'
```

要在下一個安全邊界暫停：

```python
>>> c.pause()                         # 立即返回，只提出 cooperative request
True
>>> h.wait_until_paused(timeout=120) # 確認真的抵達 paused
True
>>> c.resume()
True
```

已在 `ready`／`waiting` 時會立即暫停；模型 request 已開始就等該 Step 完成，tool batch 已開始就
等整批 tools 完成。agentloop 不會切斷執行到一半的 operation。

不再需要這個 Round 時要明確結束，避免背景 thread 永遠停在 `waiting`：

```python
>>> c.end(reason="operator")
True
>>> result = c.join(timeout=30)
>>> result.state, result.stop
('completed', 'operator')
```

`join(timeout=...)` 的 timeout 會拋出 `TimeoutError`，不是回傳 `False`。若 Round 仍在
`waiting`／`paused` 且沒有先 `resume()`、`send()` 或 `end()`，`join()` 會繼續等。

## 在 REPL 修改公開狀態

Handle 刻意暴露資料。跨 thread 一次修改多個欄位或 nested collection 時使用 `edit()`：

```python
>>> with h.edit():
...     h.dispatch["new_tool"] = new_tool
...     h.ask_options["tool_choice"] = "required"
...     h.tool_results["call_1"] = "人工覆寫的結果"
```

修改資料本身不會喚醒 runner，完成後仍要明確 `c.resume()` 或在 parked state 用 `c.send()`。
操作者必須自行承擔 schema／dispatch 不一致、竄改 history 或重做副作用的風險。

需要在 operation 邊界自動檢查時，可以在啟動前傳入自己的 Handle：

```python
>>> def inspect_results(handle):
...     print(handle.tool_results)
...     return agentloop.CONTINUE
...
>>> h = Handle(auto_finish=False)
>>> h.after_tools.append(inspect_results)
>>> c = setup.session("檢查專案", handle=h)
```

callbacks 在 runner thread 持有 Handle 的 `RLock` 同步執行；不要在 callback 裡啟動另一條需要
同一 Handle 的 thread 後立刻 `join()`，否則可能互相等待。

## 一段可直接照做的完整流程

以下只假定 `lm-gemma-4-12b` 的本機 proxy 此刻在線；在公司可把 `setup` 換成前面的 Ollama
`Engine` 寫法。先從不給工具的唯讀對話開始，確認連線後再加 tools：

```python
>>> setup = assistant(
...     "lm-gemma-4-12b",
...     system="回答精簡；不知道就明說。",
... )
>>> c = setup.session("只回答 REPL_CONNECT_OK")
>>> if not c.wait(timeout=120):
...     c.end(reason="operator-timeout")
...     raise TimeoutError("模型在 120 秒內沒有抵達可操作狀態")
...
>>> h = c.handle
>>> print(h.now())
>>> if h.state == "error":
...     print(repr(h.err))
... elif h.state in {"waiting", "paused"}:
...     print(h.message)
...     c.send("現在用一句中文說明剛才做了什麼", finish=True)
...
>>> if h.state not in {"completed", "error"}:
...     c.wait(timeout=120)
...
>>> if h.state not in {"completed", "error"}:
...     c.end(reason="operator-timeout")
...
>>> result = c.join(timeout=30)
>>> print(result.now())
>>> print(result.message)
```

確認模型與 endpoint 正常後，再建立有工具的新 Round：

```python
>>> base_tools.set_root(".")
>>> setup = assistant(
...     "lm-gemma-4-12b",
...     base_tools.tools(),
...     system="先讀取查證；未經要求不要修改檔案。",
... )
>>> c = setup.session("讀 README，只列出三個已存在的事實")
>>> c.wait(timeout=180)
True
>>> print(c.handle.message)
>>> print(c.handle.tool_log)
>>> c.end(reason="operator")
True
>>> c.join(timeout=30).state
'completed'
```

一個 Handle 只能執行一個 Round，也只能有一條 runner。要開下一個 Round 就建立新的 session；
若沿用同一個 `setup.bot`，它的 history 會延續，所以也不要同時用同一個 bot 跑兩個 session。

## 錯誤排查

### 啟動時找不到 `shells`

你可能位於 repository 根目錄但沒有設定 module path。PowerShell 先執行：

```powershell
$env:PYTHONPATH = "freepy"
python -m shells repl
```

或先 `Set-Location freepy` 再執行 launcher。

### `ModuleNotFoundError: openai`

確認 `freepy/.venv` 已建立且依賴已安裝。launcher 找不到該 venv 時會退回目前 Python；此時目前
environment 也必須裝有 FreePy 所需依賴。

### `Connection refused`、timeout 或 `h.state == "error"`

先看真正錯誤：

```python
>>> h.state
'error'
>>> repr(h.err)
```

再確認你選的是當下環境：家中 `lm-gemma-4-12b` 要有 localhost proxy；公司 Ollama 要在公司
網路且 `192.168.1.146:11434` 可達。兩者都不是永遠在線。也要核對 Ollama `/api/tags` 或
proxy 的 model list，確認模型名稱仍存在。提高 timeout 只能處理慢速載入，不能修好離線 endpoint。

### `c.wait(...)` 回 `False`

這只是 timeout；先用 `h.state`／`h.now()` 看 runner 在哪裡。不要立刻再開第二條 runner。
若決定不等，呼叫 `c.end()`；合作式 end 仍會讓已開始的模型請求或 tool batch 走到安全邊界。

### `Controller.send() requires a waiting or paused Round`

先 `c.wait()`，再檢查 state。Round 若已 `completed`／`error` 就不能重開，請建立新 session；
若仍是 `running_step`／`running_tools`，等到 parked boundary 後再送。

### `join()` 一直等或拋 `TimeoutError`

互動 session 預設停在 `waiting`。要繼續用 `send()`，要自然完成就用
`send(..., finish=True)`，不要了就 `end()`，之後才 `join()`。

### 模型沒有呼叫工具

先確認 `setup.bot.tools` 不是空的、工具已明確傳給 `assistant()`，以及所選模型／endpoint 支援
tool calling。模型能力未知時 FreePy 會放行，但這不保證後端真的做得好；小模型不呼叫工具也
可能只是模型品質問題。從一個簡單、可驗證的唯讀工具任務開始。

## 能力邊界

- REPL 不是 sandbox、permission system、scheduler 或 durable task service。
- Handle 只供同一 process 內的 threads 共用，不支援跨 process 傳遞。
- paused／waiting Round 會持續占用原 runner thread。
- pause／end 都是合作式控制，不能強制中止已開始的 endpoint request 或工具。
- 外部修改 Handle 公開資料時，資料一致性與 effect 安全由操作者負責。

核心狀態與 callback 語意見 [Round 與 Step](../../../freepy/agentloop/ROUNDS.md)，Controller API
見 [Controller](../../../freepy/agentloop/CONTROLLER.md)，背景 thread 細節見
[`agentloop.threading`](../../../freepy/agentloop/threading/README.md)。
