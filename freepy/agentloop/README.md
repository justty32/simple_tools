# agentloop

一個同步控制迴圈：執行 Step、提交狀態、通知 callbacks、執行工具，再重複。

一次 `ask() → message` 是一個 Step；工具批次發生在兩個 Steps 之間：

```text
ask() → message/tool calls
      → after_step callbacks
      → tools
      → after_tools callbacks
      → 下一次 ask(tool results)
```

完整時間／狀態語意見 [ROUNDS.md](ROUNDS.md)；runner ownership 與中止見
[RUNNER.md](RUNNER.md)。

## 最小使用方式

```python
import agentloop

h = agentloop.run(bot, dispatch, "把 a.txt 改好")
print(h.text, h.stop)
```

核心 `run()` 不建 thread，也沒有 async 版本。它永遠在呼叫者所在的 thread 執行。

## 內置方便工具

核心以外有兩個可選子專案：

- [`agentloop.threading`](threading/README.md)：把同步 `run()` 放進一條背景 thread。
- [`agentloop.limits`](limits/README.md)：用 callbacks 實作合作式預算與工具 policy。

不另造 CLI 也可以直接在 Python 互動環境持有 Handle。REPL 目錄後續也會
收納把其他 coding agent 當作操作入口的文件、腳本與範例；見
[`agentloop` 互動入口](../../docs/freepy/interfaces/python-repl.md)。

兩者都只使用核心的公開 API；核心 `run()` 不認識它們，也不替它們保留特殊分支。

## Callback 邊界

Handle 上有兩個公開 callback lists：

```python
def inspect_calls(h):
    # message 已提交，tools 尚未執行
    h.tool_calls[0]["args"]["path"] = "other.txt"
    return agentloop.CONTINUE

def rewrite_results(h):
    # 整批 tools 已完成，下一個 Step 尚未開始
    h.tool_results["call_1"] = "修改後的結果"

h = agentloop.Handle()
h.after_step.append(inspect_calls)
h.after_tools.append(rewrite_results)
agentloop.run(bot, dispatch, handle=h)
```

Callback 回 `CONTINUE`（`0`）、`PAUSE` 或 `END`。同一邊界的 callbacks 一定全部執行，
後面的看得到前面的修改；最後按 `END > PAUSE > CONTINUE` 彙總。

## 公開狀態

Handle 刻意直接暴露狀態，包含：

- `prompt`、`images`、`tools`、`dispatch`、`ask_options`
- `message`／`text`、`tool_calls`、`tool_results`、`reply`、`history`
- `state`、`step`、`usage`、`input_tokens`、`output_tokens`、
  `cached_input_tokens`、`tokens`、`calls`、`used`、`tool_log`
- `auto_finish`、`stop`、`err`

外部可以直接增刪查改；資料修改不會自動要求 agentloop 多跑一步。跨 thread 要原子修改
多個欄位或 nested list/dict 時使用：

```python
with h.edit():
    h.prompt = "繼續檢查"
    h.ask_options["tool_choice"] = "required"
```

不用 `edit()` 仍可修改，但跨 thread 的一致性由呼叫者自行承擔。已開始的模型請求或
工具批次使用啟動前取得的輸入快照；執行中的修改只影響下一個 operation。

## 自然等待、暫停與恢復

預設 `auto_finish=True`：模型不再要求工具時，Round 進入 `completed`，`run()` 返回。

設定 `auto_finish=False` 後，模型自然靜止時進入 `waiting`。原本的 runner thread 停在
Condition 裡，`run()` 不返回；另一條 thread 修改狀態後呼叫 `resume()`，同一 runner
繼續執行：

```python
h = agentloop.Handle(auto_finish=False)

# 另一條 thread 正在執行 agentloop.run(..., handle=h)
h.wait_for_state("waiting")
with h.edit():
    h.prompt = "再做一件事"
    h.auto_finish = True
h.resume()
```

`pause(safe=True)` 立即返回，且不切斷 operation：

- 已在 `ready`／parked 安全邊界：立即成為 `paused`。
- Step 中收到 pause：Step 與 `after_step` 完成後暫停，不執行 tools。
- tool batch 中收到 pause：整批 tools 與 `after_tools` 完成後暫停，不開始下個 Step。

running 時可用 `wait_until_paused()` 等待要求真正生效。`resume()` 只在 `waiting`／`paused`（或取消
尚未生效的 pause）時回 `True`；其他狀態是明確的 no-op。

一個 Handle 同時只准一個 runner，且只能用於一個 Round。Thread pool、scheduler 與
parked thread 的資源成本由使用者或上層 runtime 負責。

不想自行寫啟動與收尾樣板時，可以用內置的 thread 工具：

```python
from agentloop.threading import start

runner = start(bot, dispatch, "開始")
h = runner.handle

h.pause()
h.wait_until_paused()
# 修改公開狀態後：h.resume()
# 不打算再繼續：h.end()

result = runner.join()  # 等待、重拋 runner 啟動錯誤、返回同一個 Handle
```

`start()` 每次只建立一條 thread，不是 pool 或 scheduler。核心的同步模型與
parked-runner ownership 都沒有因此改變。

`end(safe=True, reason="ended")` 是 controller 對應 callback `END` 的操縱桿。
若 Round 正在執行 Step，它會在 Step 與 `after_step` 完成後、tools 開始前
結束；若正在執行 tool batch，則完成整批與 `after_tools` 後結束。已在
`ready`／`waiting`／`paused` 安全邊界時會立即結束，並在需要時喚醒 runner。

## `advance()` 與 Controller

`run()` 是便利迴圈；若上層只要執行一個 safe boundary，可使用 `advance()`。第一次提供
bot 與輸入，後續只傳同一個 Handle：

```python
h = agentloop.Handle()
agentloop.advance(bot, dispatch, "開始", h)  # 一次 Step
agentloop.advance(handle=h)                  # 下一次才跑一整批 tools 或 Step
```

`advance()` 不會在 `waiting` 或 `paused` 阻塞。`agentloop.Controller` 則是上面的本地方便
封裝：它收好 bot、初始輸入和同一個 Handle，提供 `.advance()`、`.run()` 或單一背景 thread 的
`.start()`。它不取代 Handle，也不宣稱是安全或耐久化 service；完整邊界見
[CONTROLLER.md](CONTROLLER.md)。

## Limits

`Limits` 位於 `agentloop.limits`，是附加在 `after_step` 的便利 callback policy：

```python
from agentloop.limits import Limits

h = agentloop.Handle()
Limits(
    steps=20,
    calls=40,
    per_tool={"run_shell": 5},
    tools=["read_file", "run_shell"],
    engines=["deepseek-chat"],
    seconds=300,
    input_tokens=200_000,
    output_tokens=20_000,
).attach(h)
agentloop.run(bot, dispatch, "...", handle=h)
```

OS 的 CPU、記憶體、網路與檔案限制不屬於這項工具；見 [LIMITS.md](LIMITS.md)。

## 驗證

```bash
cd freepy
PYTHONPATH=llmkit uv run python -m agentloop
```

離線關卡使用假的 endpoint 回應配合真的 `llms.Reply`，涵蓋 callback 改寫、公開狀態、
parked runner、跨 thread pause/resume、tool batch 邊界、錯誤與 Limits policy。
