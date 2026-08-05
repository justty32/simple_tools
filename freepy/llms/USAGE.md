# 用法

先看 [README.md](README.md) 把 proxy 跑起來。這份講串流、思考、工具、圖片。

共通約定：所有對外的方法都回傳 `(result, err)`，**絕不丟例外**，`err` 是 None 才看 `result`。

## 串流

疊代 handler 一次吐一個字，只吐「答案」的字：

```python
handler, err = bot.ask("寫首詩", stream=True)
for ch in handler:
    print(ch, end="", flush=True)
print(handler.err)        # 串流中途爆掉的話在這裡，疊代本身不會丟例外
```

`handler.text` 會把剩下的串流跑完再回傳完整文字。`with` 或 `close()` 可以提前收工，
已經收到的部分照樣寫回歷史。

## 思考

思考模型的 `reasoning_content` 不會混進答案裡，分開放：

```python
reply, err = bot.ask("9.11 和 9.9 哪個大？", model="deepseek-reasoner")
print(bot.last_reasoning)        # 非串流：上一次的思考，沒有就是 None

handler, err = bot.ask("...", stream=True)
handler.reasoning                # 串流：邊收邊累積，疊代到一半就能讀
```

思考內容**不寫回歷史** —— DeepSeek 這類 API 不收回傳的 `reasoning_content`。

DeepSeek 和 LM Studio（qwen3.5、gemma-4）都是走 `reasoning_content` 這個欄位，
不是把 `<think>` 塞在答案裡，所以答案拿到手就是乾淨的。

### 要它想 / 不要它想

**一律換模型名字**，兩家都一樣，`ask()` 本來就吃 per-call 的 `model`，
同一段對話裡混著切沒問題：

```python
bot = LLM(model="deepseek-chat")                        # 平常不想
bot.ask("難的題目", model="deepseek-reasoner")           # 這句想一下

bot = LLM(model="lm-gemma-4-e4b")                       # 預設會想
bot.ask("簡單的問題", model="lm-gemma-4-e4b-nothink")    # 這句不要想
```

底層其實是兩回事，只是被 `litellm.yaml` 包成同一種用法：DeepSeek 的 chat / reasoner
是同一顆 v4-flash 的兩個模式，本來就只能靠名字切；LM Studio 那邊三顆各有一個
`-nothink` 分身，同一顆模型，差別只是設定裡焊死了 `reasoning_effort: none`。

要臨時調思考的多寡（而不是全關），LM Studio 的可以直接送參數：

```python
bot.ask("難題", params=Params(extra={"reasoning_effort": "high"}))
```

DeepSeek 不吃 `reasoning_effort`，給了也沒用，只能換名字。

注意 `supports_reasoning: true` 只表示「這模型會思考」，**不表示每次都思考**。
gemma-4-e4b 這種混合式的模型自己決定要不要想，不想的時候回應裡根本沒有
`reasoning_content` 欄位，`last_reasoning` 就是 `None` —— 這是正常的，不是管線斷了
（順帶一提，實測它偷懶不想的那幾次，答案也真的比較容易錯）。

## 工具

schema 從函式本體生出來，靠 type hints 和 docstring：

```python
from llms import LLM, to_tools

def get_weather(city: str, unit: typing.Literal["celsius", "fahrenheit"] = "celsius"):
    """查詢指定城市的天氣。

    Args:
        city: 城市名稱
        unit: 溫度單位
    """
    ...

schemas, dispatch = to_tools(get_weather)

calls, err = bot.ask("台北天氣？", tools=schemas)
# calls: [{"id": ..., "name": "get_weather", "args": {"city": "台北", ...}}]
results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in calls}
reply, err = bot.ask(tool_results=results, tools=schemas)
```

模型要叫工具時 `ask()` 回的是 list 而不是字串，用 `isinstance(result, list)` 分辨。
`args` 的 JSON 壞掉不會丟例外，會給空 dict 並附上 `args_raw`。

串流也收得到工具，碎片會依 index 拼回同樣的形狀（讀它會先把串流跑完）：

```python
handler, err = bot.ask("台北天氣？", stream=True, tools=schemas)
handler.tool_calls
```

工具回合的歷史會連 `tool_calls` 一起寫回去，所以第二輪送 `tool_results` 時 API 對得起來。

`to_schema(fn)` 只要單一 schema，`to_schemas(*fns)` 要一串，`to_tools(*fns)` 多給一份
name → function 對照表。Google style 的 `Args:`、Sphinx 的 `:param x:`、
還有最寬鬆的 `name: 說明` 三種 docstring 格式都認。

## 圖片

```python
bot.ask("這是什麼顏色？", images=["red.png", "https://example.com/dog.jpg"])
```

本機檔案會轉成 base64 data URI，http(s) 網址直接原樣送。

## 能力檢查

`supports_tools` / `supports_vision` / `supports_reasoning` 回 `True` / `False` /
`None`（proxy 沒說）。

只有明確 `False` 才會擋下呼叫並回一個 `ValueError`，`None` 一律放行。
`supports_reasoning` 純粹是情報，不擋任何東西 —— 思考不是送出去的參數，是模型自己的事，
知道它會思考才值得去讀 `last_reasoning`。

答案來自 proxy 的 `/model/info`，也就是 `litellm.yaml` 裡的 `model_info`
加上 litellm 內建的模型資料庫。兩個都不完全可靠：內建資料庫不認得本機模型（一律 `None`），
對雲端模型也可能過期 —— 例如它說 `deepseek-reasoner` 不支援 tool calling，
但實際打過去是會叫工具的，所以 `litellm.yaml` 裡手動覆寫成 `true`。
不確定就自己實測一次再宣告。

查到的結果會快取，改完 `litellm.yaml` 重啟 proxy 後呼叫 `LLM.clear_caps_cache()` 清掉。
臨時要蓋掉某個判斷，建 instance 時給 `caps`：

```python
bot = LLM(model="lm-qwen3.5-9b", caps={"tools": True, "vision": False})
```

## 一次性問答

不需要記憶就用 module 層的 `ask()`，建一個用完即丟的 `LLM`：

```python
from llms import ask
reply, err = ask("http://localhost:4000", "翻譯這句", model="deepseek-chat")
```
