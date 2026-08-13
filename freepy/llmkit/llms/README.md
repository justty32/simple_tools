# llms

## Preset

不想每次手寫 endpoint、model 和 parameters，可以直接按 id 載入：

```python
from llms import Bot, load_preset

bot = Bot(load_preset("lm-qwen3.5-9b"))
```

資料只有一份 [`presets.json`](presets.json)：最外層是一個以 id 為 key 的 object，每筆
只含 `endpoint`、`model`、`parameters`，以及可省略的 `description`。能力仍由
`LLM` 向 proxy 查詢，不在 preset 重複維護。

包一層薄薄的殼在 openai SDK 外面，對著本機的 LiteLLM proxy 講話。

`LLM` 與 `Bot` 分工如下：

| | 是什麼 |
|---|---|
| `LLM` | endpoint、model、生成參數與模型能力 |
| `Bot` | system prompt、history、完整 tools schema／dispatch，以及一顆 `LLM` |

低階 `bot.ask()` 只說話和開口要工具；工具誰去跑、跑出什麼，由你決定後餵回來。
互動用的 `bot.start()` 則把 Bot 已驗證的 dispatch 交給 agentloop。
proxy 把 DeepSeek 雲端、遠端 Ollama、本機 LM Studio 都收成同一個 OpenAI 相容端點，
換模型就是換一個字串。

只做這件事：不做重試、不做 logging、不做 CLI。preset 只是可選的 LLM 輸入，
不承載模型知識庫或自動更新機制。

串流、思考、圖片、後設這些用法在 [USAGE.md](USAGE.md)，工具和能力在 [TOOLS.md](TOOLS.md)。

## 兩件事要先跑起來

**1. LiteLLM proxy** —— 設定檔、啟動腳本、目前有哪些模型，全在 [`../proxy/`](../proxy/README.md)。

```bash
../proxy/start_litellm.sh
```

這個 package 預設打 `http://localhost:4000`，也就是那個 proxy。要直接打別的
OpenAI 相容端點也行，`LLM(url=..., key=...)` 換掉就是，proxy 不是必需品。

**2. 這個 package 本身**只依賴 `openai`：

```bash
cd freepy/llmkit               # 這一層才 import 得到 llms
uv run python -m llms._checks_reply  # Reply shape 離線檢查，不連模型
uv run python -m llms          # 跑一遍煙霧測試
uv run python -m llms lm-gemma-4-e4b
```

`python -m llms` 會依序試對話記憶、串流、思考、工具、後設，五關都印出來。
第一個參數是一般模型，第二個是思考模型（預設 `deepseek-chat` / `deepseek-reasoner`）。

改完 proxy 的設定要重啟它，程式這邊則呼叫 `LLM.clear_caps_cache()` 清掉能力快取
—— 能力表是照 proxy 根位址快取的，查不到的空表也算查過，不會自動重試。

## 最小用法

```python
from llms import Bot, LLM, Params

bot = Bot(
    LLM(model="deepseek-chat", params=Params(temperature=0.2)),
    system="你是個惜字如金的助手",
)

reply = bot.ask("你好")
print(reply.text)
if not reply:
    print(reply.err)
```

`ask()` **永遠回傳一個 `Reply`，絕不丟例外**。錯誤在 `reply.err`，`bool(reply)` 是 `False`。

### Reply

一步的結果，串流與否都是它：

| | |
|---|---|
| `.text` | bot 說了什麼 |
| `.calls` | 它要你去做什麼，`[{"id", "name", "args"}]` |
| `.reasoning` | 它想了什麼，沒想就是 `None` |
| `.finish_reason` | 為什麼停：`"stop"` / `"length"`（**話被切斷了**）/ `"tool_calls"` |
| `.usage` | `{"prompt", "completion", "total", "cached", "reasoning"}`，缺的是 `None` |
| `.err` | 壞了沒 |

`.text` 和 `.calls` 可以同時有東西 —— 模型常常一邊說「好，我查一下」一邊叫工具。

### Bot / LLM

```python
Bot(llm=None, system=None, tools=None)
LLM(model="deepseek-chat", url="http://localhost:4000",
    key=None, params=None, timeout=60, caps=None)
```

- `system` 不佔歷史，每次送出時才補在最前面；`reset()` 清記憶不動它
- `bot.pending_calls` 是它要了、你還沒餵結果回去的工具呼叫（見 [TOOLS.md](TOOLS.md)）
- `tools` 接 callable、callable 集合或 `(schemas, dispatch)` bundle；名稱與執行端必須完整配對
- 換引擎用 `bot.set_llm(LLM(...))`；只換模型直接 `bot.llm.model = "..."`
- `bot.start(instruction)` 啟動背景 Round；同一個 mutable Bot 不允許並行 Round
- `url` 給 base url 或整條 `/chat/completions` 都行，會自己正規化
- `key` 沒給就吃 `OPENAI_API_KEY`，再沒有就用 `"hello"` 頂著（本機 proxy 不檢查）
- `caps={"tools": True, "vision": False}` 可以蓋掉 proxy 回報的能力
- `Params` 只吐出有設定的欄位，其餘交給模型自己的預設值

`Params.extra` 是**直接展開成 `create()` 的參數**，所以 key 必須是 openai SDK 有的：

```python
Params(extra={"reasoning_effort": "none"})                     # OK
Params(extra={"chat_template_kwargs": {...}})                  # TypeError
Params(extra={"extra_body": {"chat_template_kwargs": {...}}})  # 非標準的要這樣包
```

第二行會回 `TypeError: got an unexpected keyword argument`。因為 `ask()` 不丟例外，
它會安靜地變成 `reply.err` —— 不看 `err` 的話你只會拿到一個空的 `text`，很難查。

## 檔案分工

| 檔案 | 負責 |
|---|---|
| `client.py` | `Bot`：人格、記憶、能力、`ask()`／`start()` |
| `engine.py` | `LLM`：端點、模型、旋鈕、這端點做得到什麼 |
| `interactive.py` | 背景 Round 啟動與單一 active Round ownership |
| `toolset.py` | tools schema／dispatch 的組裝與驗證 |
| `reply.py` | `Reply`：一步的結果，串流與否都是它 |
| `usage.py` | usage 物件 → 純 dict |
| `toolcalls.py` | tool_calls 在 raw / history / entries 之間搬，含串流碎片的 `Accumulator` |
| `caps.py` | 問 proxy 這顆模型做得到哪些事，含快取 |
| `content.py` | url、key、圖片這些雜事，全是純函式 |
| `params.py` | `Params`：只吐出有設定的呼叫參數 |
| `func_schema.py` | python 函式 → OpenAI tool schema |
| `jsontypes.py` | type annotation → JSON schema type |
| `docstrings.py` | 從 docstring 挖描述 |
| `__main__.py` | `python -m llms` 的煙霧測試 |

一個檔一件事，程式和文件都在 150 行以內。
