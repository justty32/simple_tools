# agentloop

**一個函式，讓 bot 自己一直跑。**

一次完整 `run()` 是一個回合（Round）；裡面每次 `ask() → message` 是一步（Step）。
回合內重新配置下一個 Step 與控制邊界的完整語意在 [ROUNDS.md](ROUNDS.md)。

底下兩層（[`llms`](../llmkit/llms/README.md)，含可選 preset；以及
[`tooljson`](../llmkit/tooljson/README.md)）都是「人推一步，它動一下」。
這一層負責**真的去執行、把結果餵回去、決定什麼時候收手**。

```
模型說「我要用 read_file」
    → 這裡真的去跑
    → 結果餵回去
    → 它再想一步
    → …直到它不叫工具了，或預算用完
```

## 三行

```python
import agentloop, base_tools
from llms import LLM

schemas, dispatch = base_tools.tools()
bot = LLM(tools=schemas)

h = agentloop.run(bot, dispatch, "把 a.txt 裡的 two 改成 TWO")
print(h.text, h.stop)
```

`h` 是把手（`Handle`），跑完之後它就是這趟的報告。

## 阻塞 API

`run()` 會阻塞到整個 Round 結束。它不建 task、不開 thread，也沒有 async 版本：

```python
h = agentloop.Handle()
result = agentloop.run(bot, dispatch, "整理一下這個資料夾", h)
```

需要一邊跑一邊控制時，由上層 runtime 把這個阻塞函式放進 worker thread；
`agentloop` 本身不管這層排程。另一條 thread 可拿同一個 `Handle` 查看或控制。

把手上有兩組東西：

**問 live 狀況**（普通同步函式，可跨 thread）：`h.now()` 一行人話、
`h.done()` 收工了沒、`h.elapsed()` 花了幾秒。完成後可穩定讀取 `h.text`、`h.stop`、
`h.step`、`h.tool_log`、`h.calls` / `h.used` / `h.tokens` 等報告欄位。

**改下一步**：`h.add_instruction("…")`（短名 `h.say("…")`）、
`h.add_images("photo.png")`、`h.add_tools(schemas, dispatch)`，以及
`h.set_ask_options(tool_choice="required", stream=True)`。ask options 會持續到再次
修改；圖片只送下一步，工具則從下一步起加入 bot 能力與執行表。
若 `Limits.tools` 設了白名單，新工具仍須在白名單內才會真正執行。

**下控制**：`h.pause()` / `h.resume()`、`h.ask_stop()`。

這些變更都是**下一步開頭生效**，不會打斷正在跑的那一步。
沒有「立刻中斷」——模型那次 HTTP 和跑到一半的工具都停不下來，
假裝停得下來只會讓人以為工具沒跑過。

`Handle` 的查詢與控制方法可從其他 OS thread 使用。多筆
instruction 依 FIFO 送出，completion 不會吃掉同時到達的指令。一個 `Handle`
只屬於一個 Round，重用或同時使用會立即失敗。競態、拒絕訊息與 pause/stop
的 settlement 規則見 [ROUNDS.md](ROUNDS.md)。

## 為什麼停

狀態機只分正常的 `completed` 與非正常的 `error`。`h.stop` 保留底下的具體原因：

| | |
|---|---|
| `done` | 模型不叫工具了，講完了。**正常結束** |
| `length` | 也不叫工具了，但它是被 `max_tokens` 切斷的，不是講完 |
| `budget` | Step 預算用完，它還在叫工具 |
| `calls` / `time` / `tokens` | 那項預算用完 |
| `engine` | 它掛的思考引擎不在准用清單裡 |
| `error` | `llms` 那邊回錯，看 `h.err` |
| `stopped` | 你叫它收手 |

`done` 和 `length` 分開，是因為「它沒再叫工具」有兩種完全不同的原因：
**講完了**，跟**話被切斷所以看起來像講完了**。混在一起的話，
一個被截斷的回答會被當成正常結束。

## 預算

給它多少步、多少工具、多少時間、哪些工具、哪些引擎 —— 全在
[LIMITS.md](LIMITS.md)。那份也講**沒做的那一半**（cpu / gpu / 記憶體 / 網路 /
能碰哪些檔案），以及為什麼那些不放進 `Limits` 裡假裝有。

```python
agentloop.run(bot, dispatch, "…", limits=agentloop.Limits(
    steps=20, calls=40, per_tool={"run_shell": 5},
    tools=["read_file", "run_shell"], engines=["deepseek-chat"],
    seconds=300, tokens=200_000))
```

不給就是 `Limits()`：12 步，其餘不限。
計數型限制在建構時就驗證：`steps` / `quiet` 要正整數，其餘次數要非負。
`seconds` 和 `tokens` 都是**合作式限制**：只在安全邊界檢查，最多可超過一次
model/tool operation，不是 hard kill。

## 停在一半，可以接著跑

Step／其他預算在下一批 tools 開始前用完，或 stop 在 model 回應時到達，
那批工具**還沒跑**，債留在 `bot.history` 上。同一個 bot 用新 Handle
再叫一次就從那裡接下去：

```python
h = agentloop.run(bot, dispatch, "…", limits=Limits(steps=5))
if h.stop == "budget":
    h = agentloop.run(bot, dispatch)     # 不用再給 prompt
```

迴圈每一步都從「先還上一步欠的工具結果」開始，所以「接著跑」
跟平常那一步走同一條路。若 stop 是在 tool batch 中到達，當批已執行的
results 會先 settlement，下個 Round 不會重跑這些副作用。

## 工具壞掉不會打斷迴圈

不存在的工具、參數不匹配、非法 JSON 與工具例外都變成一句 `Error: ...`
送回模型，讓它改正後再試。參數會先用函式簽名驗證，工具內部的
`TypeError` 不會被誤報成參數錯誤；過長輸出也會截斷以保護 context。

## 它不做什麼

- **不對外廣播串流。** 可以把 `stream=True` 設成 ask option，但 `run()` 仍會在
  內部收完。要逐字顯示就自己 `bot.ask(stream=True)`。
- **不 import `llms`，也不 import `tooljson`。** `bot` 只要有 `ask()` 和
  `pending_calls`，`dispatch` 只要是 `{名字: fn(**kwargs)}` ——
  `base_tools.tools()` / `tooljson.tools()` / `llms.to_tools()` 生出來的都是
  這個形狀，混著給也行。
- **工具一次跑一個**，照模型要求的順序。併發省不了多少，
  卻會讓兩個 `run_shell` 在同一個資料夾裡互相踩。

## 驗

```bash
cd freepy
PYTHONPATH=llmkit uv run python -m agentloop                  # 離線關卡
PYTHONPATH=llmkit uv run python -m agentloop deepseek-chat    # 再多一關：真模型
```

離線那部分用**假的回應物件餵真的 `Reply`**，所以迴圈走的路跟接真模型時一模一樣，
差別只在 `ask()` 不出門。壞掉的模型、壞掉的工具、每一種預算、競態邊界、
跨 thread FIFO、pause/stop、one-shot Handle 和接著跑，都在裡面。
