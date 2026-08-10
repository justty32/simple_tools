# 工具與能力

bot 的**能力**（它能開口要求哪些工具）和**引擎的能力**（這個端點做得到哪些事）。
說話本身的用法在 [USAGE.md](USAGE.md)。

## 工具

schema 從函式本體生出來，靠 type hints 和 docstring。**工具是 bot 的能力，掛在
bot 上，不是每次呼叫的參數** —— 順便讓工具定義穩定待在前綴裡，快取才命中得了。

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
bot = LLM(tools=schemas)

reply = bot.ask("台北天氣？")
print(reply.text)      # 它常常會一邊說「好，我查一下」一邊叫工具
print(reply.calls)     # [{"id": ..., "name": "get_weather", "args": {...}}]

results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in reply.calls}
print(bot.ask(tool_results=results).text)
```

**bot 只開口要工具，執行是你的事。** 它不知道也管不著工具是誰跑的、跑多久、
跑失敗了沒 —— 你餵什麼回去它就信什麼。

`tool_results` 就是「跟它說：你要的那些工具跑出了這些」，所以它跟 `prompt` 是同一個
動作的兩種材料，可以一起給（順序是 `tool_results` 先、`prompt` 後）。

`args` 的 JSON 壞掉不會丟例外，會給空 dict 並附上 `args_raw` —— 小模型和被
`max_tokens` 切斷的輪都會這樣，**執行前記得看一眼有沒有 `args_raw`**。

工具輪的記憶會連 `tool_calls` 一起寫回去，所以第二輪送 `tool_results` 時 API 對得
起來。串流也收得到工具，碎片會依 index 拼回同樣的形狀（讀 `reply.calls` 會先把串流
跑完）。

一輪可能吐出**好幾個**呼叫，順序和成敗都由你決定；全部跑完再一次餵回去，
`tool_results` 的 key 少一個下一輪就對不起來。

### 欠著的工具呼叫

`ask()` 只有一個動作（說話和餵結果都是「跟它說話」），代價是**沒有東西擋著你在
還欠工具結果的時候就講下一句**。帶 `tool_calls` 的 assistant message 後面一定要接上
對應的 tool message，跳過就會被 API 打回票。要確認欠了什麼：

```python
if bot.pending_calls:          # 沒欠就是空 list
    ...
```

形狀跟 `reply.calls` 一樣。它是從 `history` 最後一則讀出來的，所以 `reset()` 之後
自然歸零。

### tool_choice：逼它叫工具

| 值 | 意思 |
|---|---|
| `"auto"` | 自己決定要不要叫（預設） |
| `"none"` | 不准叫，只准說話 |
| `"required"` | 一定要叫至少一個，不准只說話 |
| `{"type": "function", "function": {"name": "plan"}}` | 指名必須叫這一個 |

```python
bot.ask("台北天氣？", tool_choice="required")
```

小模型很常不理你的工具、直接講一段話。與其在 prompt 裡拜託它（「請用工具回答」），
不如用 `"required"` —— 那是在解碼層直接不准它輸出普通文字，是**強制**不是**拜託**。
前提是端點吃這個參數，`tool_choice` 那格能力明確 `False` 的話會被擋下來。

沒有 `tools` 卻給 `tool_choice` 會回一個 `ValueError` 進 `reply.err`。它本來就送不出去
（API 不收），但**安靜吞掉一個你明明有給的參數**跟 `drop_params` 那個坑是同一回事，
所以講出來。

`to_schema(fn)` 只要單一 schema，`to_schemas(*fns)` 要一串，`to_tools(*fns)` 多給一份
name → function 對照表。Google style 的 `Args:`、Sphinx 的 `:param x:`、
還有最寬鬆的 `name: 說明` 三種 docstring 格式都認。

### 工具不一定要是 python 函式

`(schemas, dispatch)` 這個形狀就是這裡的介面，誰生的無所謂。隔壁的
[`tooljson`](../tooljson/README.md) 從 .json 生出同樣的東西（工具是外部執行檔或
HTTP API 而不是 python 函式），兩邊混著給也可以：

```python
a, da = to_tools(get_weather)
b, db = tooljson.tools("mytools.json")
bot = LLM(tools=a + b)
dispatch = {**da, **db}
```

## 引擎的能力

能力掛在引擎上，因為「能不能看圖」是端點加模型的性質，跟這個 bot 是誰無關：

```python
bot.engine.caps                    # 完整一張表
bot.engine.supports("vision")      # True / False / None（proxy 沒說）
```

現在查得到這幾項：

| | 是什麼 | 會擋呼叫？ |
|---|---|---|
| `tools` | 叫不叫得動工具 | 會 |
| `tool_choice` | 能不能指定 `tool_choice` | 會 |
| `vision` | 讀不讀得懂圖 | 會 |
| `parallel_tools` | 一輪能不能吐多個呼叫 | 不會 |
| `reasoning` | 會不會思考 | 不會 |
| `json_schema` | 收不收 `response_format` 的 schema | 不會 |
| `caching` | 有沒有前綴快取 | 不會 |

**只有明確 `False` 才擋**（回一個 `ValueError` 進 `reply.err`），`None` 一律放行。
不擋的那幾個純粹是情報：值不值得去讀 `reasoning`、要不要為了快取排訊息順序。

**七項都是實打過才宣告的**（2026-08-08，DeepSeek 雲端 + 本機 LM Studio 兩邊各打一次），
不是抄 litellm 的內建資料庫 —— 抄來的至少有一項是錯的：litellm 說 DeepSeek
`supports_response_schema` 是 `True`，實際送 `response_format` 回 **400
`This response_format type is unavailable now`**。`proxy/litellm.yaml` 已經覆寫成 `false`。

`ollama-*` 那批**沒驗**（那台機器連不到），所以 yaml 裡沒宣告，一律是 `None` 放行。

能力名稱打錯不會安靜地變成「不知道」：`supports("visoin")` 是 `KeyError`，
`Engine(caps={"tool": True})` 建構時就 `ValueError`。這是刻意的 —— 安靜吃掉打錯的
設定，就是 litellm `drop_params` 那個坑的翻版（見
[`../proxy/README.md`](../proxy/README.md)）。

答案來自 proxy 的 `/model/info`（`proxy/litellm.yaml` 的 `model_info` 加上 litellm
內建的資料庫），**兩個都不完全可靠，宣告前先實測** —— 理由見
[`../proxy/README.md`](../proxy/README.md)。

查到的結果會快取，改完設定重啟 proxy 後要呼叫 `Engine.clear_caps_cache()`。
臨時要蓋掉某個判斷，建引擎時給 `caps`：

```python
bot = LLM(engine=Engine(model="lm-qwen3.5-9b", caps={"tools": True, "vision": False}))
```
