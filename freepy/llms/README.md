# llms

包一層薄薄的殼在 openai SDK 外面，對著本機的 LiteLLM proxy 講話。

一個 `LLM` instance 就是一段對話：預設記住歷史，呼叫 `ask()` 不用自己組 messages。
proxy 把 DeepSeek 雲端、遠端 Ollama、本機 LM Studio 都收成同一個 OpenAI 相容端點，
換模型就是換一個字串。

只做這個 class 需要的事：不做重試、不做 logging、不做 config 檔、不做 CLI。

串流、思考、工具、圖片這些用法在 [USAGE.md](USAGE.md)。

## 兩件事要先跑起來

**1. LiteLLM proxy**（吃同目錄的 `litellm.yaml`，預設 4000 port）

```bash
./start_litellm.sh
# 等同於
uv run --with 'litellm[proxy]' --with 'fastapi<0.119' litellm --config litellm.yaml --port 4000
```

用 `uv run` 臨時把 litellm 拉進來，不用先裝也不用維護 venv。
`fastapi` 釘在 0.119 以下：新版跟 `litellm[proxy]` 目前的 pydantic 相容性會出事。
Windows 用 `start_litellm.ps1`，參數一樣是 `(config, port)`。

要用 DeepSeek 就先給金鑰：`export DEEPSEEK_API_KEY=sk-...`。
連不到的來源不會擋住 proxy 啟動，只有真的去呼叫它時才失敗。

**2. 這個 package 本身**只依賴 `openai`：

```bash
uv run --with openai python -m llms          # 跑一遍煙霧測試
uv run --with openai python -m llms lm-gemma-4-e4b
```

`python -m llms` 會依序試對話記憶、串流、思考、工具，四關都印出來。
第一個參數是一般模型，第二個是思考模型（預設 `deepseek-chat` / `deepseek-reasoner`）。

## 現在有哪些模型

model 名字定義在 `litellm.yaml`，`curl localhost:4000/v1/models` 可以確認目前實際載入的：

| 名字 | 來源 | 備註 |
|---|---|---|
| `deepseek-chat` | DeepSeek 雲端 | 舊名，現在打到 v4-flash 的非思考模式 |
| `deepseek-reasoner` | DeepSeek 雲端 | 舊名，現在打到 v4-flash 的思考模式 |
| `deepseek-v4-pro` | DeepSeek 雲端 | 會思考 |
| `lm-gemma-4-12b` / `lm-gemma-4-e4b` / `lm-qwen3.5-9b` | 本機 LM Studio | 三個都看得到圖、都會思考、都叫得動工具 |
| `ollama-*` | 遠端 Ollama @ 192.168.1.146 | 只有連得到那台時才通 |

LM Studio 沒載入模型時第一次呼叫會由它自己 JIT 載入，會慢一下。

改完 `litellm.yaml` 要重啟 proxy，程式這邊則呼叫 `LLM.clear_caps_cache()` 清掉能力快取。

## 最小用法

```python
from llms import LLM, Params

bot = LLM(model="deepseek-chat", params=Params(temperature=0.2, max_tokens=200))
reply, err = bot.ask("你好")
```

所有對外的方法都回傳 `(result, err)` 這種 Go 風格的 tuple，**絕不丟例外**；
`err` 是 None 才看 `result`。

`LLM(url="http://localhost:4000", model=..., key=None, system=None, params=None, timeout=60, caps=None)`

- `url` 給 base url 或整條 `/chat/completions` 都行，會自己正規化
- `key` 沒給就吃 `OPENAI_API_KEY`，再沒有就用 `"hello"` 頂著（本機 proxy 不檢查）
- `system` 不佔歷史，每次送出時才補在最前面；`reset()` 清歷史不動它
- `caps={"tools": True, "vision": False, "reasoning": True}` 可以蓋掉 proxy 回報的能力
- `Params` 只吐出有設定的欄位，其餘交給模型自己的預設值；`extra` 直接併進 kwargs

## 檔案分工

| 檔案 | 負責 |
|---|---|
| `client.py` | `LLM` class，對話狀態和 `ask()` |
| `stream.py` | 串流處理器：答案 / 思考 / tool_calls |
| `toolcalls.py` | tool_calls 的組裝與拆解，含串流碎片的 `Accumulator` |
| `caps.py` | 問 proxy 模型支不支援 tool / vision / 思考，含快取 |
| `content.py` | url、key、圖片這些雜事，全是純函式 |
| `params.py` | `Params`：只吐出有設定的呼叫參數 |
| `oneshot.py` | 不記憶的一次性 `ask()` |
| `func_schema.py` | python 函式 → OpenAI tool schema |
| `jsontypes.py` | type annotation → JSON schema type |
| `docstrings.py` | 從 docstring 挖描述 |
| `__main__.py` | `python -m llms` 的煙霧測試 |

一個檔一件事，程式和文件都在 150 行以內。
