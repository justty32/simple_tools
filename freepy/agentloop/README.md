# agentloop

**一個函式，讓 bot 自己一直跑。**

一次完整 `run()` 是一個回合（Round）；裡面每次 `ask() → message` 是一步（Step）。
回合內追加指令與工具要求使用者輸入的完整語意規劃在 [ROUNDS.md](ROUNDS.md)。

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
import asyncio, agentloop, base_tools
from llms import LLM

schemas, dispatch = base_tools.tools()
bot = LLM(tools=schemas)

h = asyncio.run(agentloop.run(bot, dispatch, "把 a.txt 裡的 two 改成 TWO"))
print(h.text, h.stop)
```

`h` 是把手（`Handle`），跑完之後它就是這趟的報告。

## 真正的用法：一邊跑，一邊看，一邊控制

`run()` 是 async 的，所以你可以把它丟著跑，另一條 routine 拿同一個把手：

```python
h = agentloop.Handle()
task = asyncio.create_task(agentloop.run(bot, dispatch, "整理一下這個資料夾", h))

while not h.done():
    await asyncio.sleep(1)
    print(h.now())              # 第 3/12 步，正在跑 run_shell，5 個工具，8420 tokens，31s
    if 我看它走偏了:
        h.say("別再讀了，直接寫檔")
```

把手上有兩組東西：

**問狀況**（都是普通函式，不用 await）：`h.now()` 一行人話、`h.done()` 收工了沒、
`h.text` 模型最後說的話、`h.stop` 為什麼停、`h.step` 已送出的模型請求數、`h.tool_log` 做過的每一件工具工作、
`h.calls` / `h.used` / `h.tokens` / `h.elapsed()` 花掉多少。

**下指令**：`h.say("…")` 插一句話、`h.pause()` / `h.resume()`、`h.ask_stop()`。

指令都是**下一步開頭生效**，不會打斷正在跑的那一步。
沒有「立刻中斷」——模型那次 HTTP 和跑到一半的工具都停不下來，
假裝停得下來只會讓人以為工具沒跑過。

## 為什麼停

`h.stop` 一定是這幾個之一：

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

## 停在一半，可以接著跑

Step 預算用完（或你喊停）的時候，最後那批工具**還沒跑**，債留在 `bot.history` 上。
同一個 bot 再叫一次就從那裡接下去，工具不會跑兩遍：

```python
h = asyncio.run(agentloop.run(bot, dispatch, "…", limits=Limits(steps=5)))
if h.stop == "budget":
    h = asyncio.run(agentloop.run(bot, dispatch))     # 不用再給 prompt
```

這是免費送的：迴圈每一步都從「先還上一步欠的工具結果」開始，
所以「接著跑」跟平常那一步走的是同一條路，沒有第二套程式。

## 工具壞掉不會打斷迴圈

四種壞法都會變成一句英文送回模型，讓它自己改一次再試：沒有這個工具、
參數對不上、工具自己炸了、**args 不是合法 JSON**（小模型和被 `max_tokens`
切斷的 Step的常客）。

判斷「參數對不對」是**先問簽名再叫**，不是叫下去接 `TypeError` ——
不然工具內部自己丟的 `TypeError` 會被誤報成「參數對不上」，
模型就照著那句假話去改一個本來沒問題的參數。

## 它不做什麼

- **不串流。** 逐字看是給人看的，這個迴圈是放著自己跑的。要逐字看就自己
  `bot.ask(stream=True)`（[`../try.py`](../try.py) 就是那樣）。
- **不 import `llms`，也不 import `tooljson`。** `bot` 只要有 `ask()` 和
  `pending_calls`，`dispatch` 只要是 `{名字: fn(**kwargs)}` ——
  `base_tools.tools()` / `tooljson.tools()` / `llms.to_tools()` 生出來的都是
  這個形狀，混著給也行。
- **工具一次跑一個**，照模型要求的順序。併發省不了多少，
  卻會讓兩個 `run_shell` 在同一個資料夾裡互相踩。

## 驗

```bash
cd freepy
PYTHONPATH=llmkit uv run python -m agentloop                  # 35 關，離線
PYTHONPATH=llmkit uv run python -m agentloop deepseek-chat    # 再多一關：真模型
```

離線那部分用**假的回應物件餵真的 `Reply`**，所以迴圈走的路跟接真模型時一模一樣，
差別只在 `ask()` 不出門。壞掉的模型、壞掉的工具、每一種預算、暫停、插話、
接著跑，都在裡面。

## 檔案分工

| 檔案 | |
|---|---|
| `loop.py` | `run()`：迴圈本身 —— 推幾步、什麼時候收手 |
| `calling.py` | 一個 tool call → 一個字串（壞掉的四種也是字串） |
| `handle.py` | `Handle`：外面問狀況、下指令的窗口 |
| `limits.py` | `Limits`：這次最多能花掉多少 |
| `LIMITS.md` | 作業系統那一層的資源限制，**還沒做** |
| `__main__.py` | 煙霧測試 |
