# proxy

一個 LiteLLM proxy 的設定檔和啟動腳本。它把 DeepSeek 雲端、遠端 Ollama、本機
LM Studio 收成**同一個 OpenAI 相容端點**（預設 `localhost:4000`），
所以換模型就只是換一個字串，程式碼不用動。

`llms` 預設就是打這裡。但它不是必需品 —— 有現成的 OpenAI 相容端點就直接
`Engine(url=..., key=...)` 指過去。

| 檔案 | 是什麼 |
|---|---|
| `litellm.yaml` | 有哪些模型、各自打到哪、各自做得到什麼 |
| `start_litellm.sh` | 啟動（Linux / macOS） |
| `start_litellm.ps1` | 啟動（Windows），參數一樣是 `(config, port)` |

## 起來

```bash
export DEEPSEEK_API_KEY=sk-...     # 只有要用 DeepSeek 才需要
./start_litellm.sh                 # 等同 ./start_litellm.sh litellm.yaml 4000
curl localhost:4000/v1/models      # 確認實際載入了哪些
```

**連不到的來源不會擋住啟動**，只有真的去呼叫它時才失敗 —— 所以沒開 LM Studio、
沒有 DeepSeek 金鑰，proxy 一樣起得來，剩下的模型照用。

litellm **不裝進任何 venv**，腳本用 `uv run --with` 臨時把它拉進來跑，不用先裝、
也不用維護第二個 venv。

### fastapi 為什麼釘在 `<0.130`

兩邊都會壞，所以要夾在 `[0.119, 0.130)` 之間：

- **`>=0.130`**：某版拿掉了 `litellm[proxy]` 內部依賴的私有 API
  （`fastapi.dependencies.utils.get_flat_dependant`），proxy 開機就 crash。
- **`<0.119`**：會讓 uv 把 litellm 反向解析回 1.79.x 那種老版本。那個版本的
  **Ollama function calling 是假的** —— 它把工具定義用文字塞進 system prompt 叫
  模型自己接龍 JSON，不走 `/api/chat` 原生 `tools`。2026-08-06 實測：三個工具以上
  就常常漏答成純文字，根本不會進 `tool_calls`。

2026-08-06 實測 `fastapi<0.130` 落在 litellm 1.87.5，開機正常、原生 tool calling
也正常。之後升版遇到新的相容性問題再調。

## 現在有哪些模型

名字定義在 `litellm.yaml`：

| 名字 | 來源 | 備註 |
|---|---|---|
| `deepseek-chat` | DeepSeek 雲端 | 舊名，現在打到 v4-flash 的非思考模式 |
| `deepseek-reasoner` | DeepSeek 雲端 | 舊名，現在打到 v4-flash 的思考模式 |
| `deepseek-v4-pro` | DeepSeek 雲端 | 會思考 |
| `lm-gemma-4-12b` / `lm-gemma-4-e4b` / `lm-qwen3.5-9b` | 本機 LM Studio | 三個都看得到圖、都會思考、都叫得動工具 |
| 上面三個各加 `-nothink` | 同一顆模型 | 關掉思考的分身，例如 `lm-gemma-4-e4b-nothink` |
| `ollama-*` | 遠端 Ollama @ 192.168.1.146 | 只有連得到那台時才通 |

LM Studio 沒載入模型時第一次呼叫會由它自己 JIT 載入，會慢一下。

**開關思考統一成「換名字」**：兩家的機制其實不同，是 yaml 把它們包成同一種用法。
DeepSeek 本來就只能換名字（也**不吃** `reasoning_effort`，2026-08-05 給 `"none"`
照樣想了 92 字）；LM Studio 吃 `reasoning_effort`，所以每顆模型多開一個 `-nothink`
分身，把 `reasoning_effort: "none"` 焊死在 `litellm_params` 裡。程式端永遠只換模型
字串，不用記哪顆用哪個旋鈕。

### `drop_params: true` 會無聲吞掉參數

這份設定開了 `drop_params: true`，而 litellm 的支援參數清單裡**沒有**
`reasoning_effort`（`/model/info` 的 `supported_openai_params` 可以查）。
於是它會被**丟掉而且不報錯、不警告** —— 你以為關掉思考了，其實沒有。

解法是那三個 `lm-*` 都加 `allowed_openai_params: ["reasoning_effort"]` 強制放行。
這是 `drop_params: true` 的一般性代價：**以後任何「參數送了沒反應」都先往這裡查。**

試過但沒用的兩招，別再繞回去：`chat_template_kwargs: {enable_thinking: false}`
（無效，思考照舊）、prompt 尾巴加 `/no_think`（反效果，它會開始思考「什麼是
/no_think」，思考字數反而翻倍）。

LM Studio 那區用 **YAML anchor**（`&lm` / `<<: *lm`）收掉重複。`-12b-nothink` 是唯一
沒有自己覆寫 `model:` 的一筆，完全靠繼承 —— 改那區之後要特別驗它有沒有打到正確的
模型（看 LM Studio 實際載入了哪顆）。

## `model_info` 是實測出來的，不要信 litellm 的內建資料庫

`/model/info` 的答案有兩個來源：`litellm.yaml` 裡手寫的 `model_info`，
和 litellm 自己帶的模型資料庫。**後者對雲端模型也會過期**：

- **2026-08-05**：litellm 說 `deepseek-reasoner` 的 `supports_function_calling` 是
  `False`，但實際打過去它照樣回 tool_calls。所以 yaml 裡覆寫成 `true` ——
  不覆寫的話 `llms` 會在本地就把你擋死，請求根本送不出去。**（謊報成 False）**
- **2026-08-08**：litellm 說 DeepSeek 的 `supports_response_schema` 是 `True`，
  實際送 `response_format` 回 **400 `This response_format type is unavailable
  now`**。yaml 覆寫成 `false`。**（謊報成 True）**
- 本機模型（Ollama、LM Studio）litellm 一律不認得，不宣告就全是 `None`。

兩個方向都會錯，所以規矩是：**要宣告某個旗標之前，先實打一次。**

2026-08-08 對 `deepseek-chat` 和 `lm-gemma-4-e4b` 各打了一輪，七項全部有結論：

| | DeepSeek | LM Studio |
|---|---|---|
| `tool_choice`（`"required"` 真的逼得出呼叫） | ✓ | ✓ |
| `parallel_function_calling`（一輪吐 2 個 call） | ✓ | ✓ |
| `response_schema` | ✗ 400 | ✓ |
| `prompt_caching`（`usage.cached` 真的有數字） | ✓ 512 | ✗ 一直是 None |

`ollama-*` 那批**沒驗**（那台機器連不到），所以刻意不宣告，維持 `None`。

## 改完設定要做兩件事

1. **重啟 proxy** —— 設定不會熱載入。
2. 程式端 `Engine.clear_caps_cache()` —— 能力表是照 proxy 根位址快取的，
   **查不到的空表也算查過**，不會自動重試。

## 踩過的

- **PowerShell 預設擋 `.ps1`**，`start_litellm.ps1` 跑不動時多半是 ExecutionPolicy
  的問題，不是腳本錯。
- **`.gitattributes` 是必要的，不是潔癖。** 沒有它，Windows checkout 會把 `.sh`
  轉成 CRLF，回到 Linux 執行就是 `bad interpreter: /usr/bin/env bash^M`。
