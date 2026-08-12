# llmkit

**freepy 的地基。** 三塊東西，各自獨立，合起來剛好夠讓一個模型真的去做事。

| | 是什麼 | |
|---|---|---|
| **[`llms/`](llms/README.md)** | 把 OpenAI 相容的 API 包成一個 **bot 模型** | 一個 lib |
| **[`proxy/`](proxy/README.md)** | LiteLLM proxy 的設定檔和啟動腳本 | 一份設定 |
| **[`tooljson/`](tooljson/README.md)** | tool JSON 的**格式規範**，和它的標準庫 | 一份契約＋一個實作 |

這裡的東西**已經定型**：介面不會隨手改，改了要有理由、要更新文件。還在長的東西
（檔案讀寫工具、工具掃描、prototype）在上一層的 [`../`](..)，它們長在這上面。

三者的關係是單向的：`proxy` 誰都不知道，`llms` 只是預設打 `proxy`，
`tooljson` 產出的東西剛好是 `llms` 收的形狀。抽掉任何一塊，另外兩塊照樣能用。

## 串起來長這樣

```python
import tooljson
from llms import LLM

schemas, dispatch = tooljson.tools("mytools.json")
bot = LLM(system="你是個惜字如金的助手", tools=schemas)

reply = bot.ask("把 a.png 縮到 800")
while reply.calls:
    results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in reply.calls}
    reply = bot.ask(tool_results=results)
print(reply.text)
```

**bot 只會說話和開口要工具，執行不歸它管。** 工具誰去跑、跑出什麼，
由呼叫端決定後餵回來 —— 這條界線是刻意的，`llms` 裡沒有任何一行會去執行東西。

## 起來

```bash
cd freepy/llmkit                        # 這一層才 import 得到 llms / tooljson
uv run python -m tooljson               # 45 關，全離線，連 openai 都用不到

export DEEPSEEK_API_KEY=sk-...          # 要打 DeepSeek 才需要
./proxy/start_litellm.sh &              # 用別的 OpenAI 相容端點就不用起 proxy
uv run python -m llms                   # 五關：記憶／串流／思考／工具／後設
```

依賴宣告在 `../pyproject.toml`（整個 freepy 共用一個 `.venv`），只有 `openai`
一個，而且只有 `llms/` 用得到 —— `tooljson/` 是純標準庫。

沒有 unit test，**驗證靠實跑**：`python -m <package>` 就是那個「跑」。

## 狀態

介面定型了，但**定型不等於什麼都驗過**。目前的實話：

介面定型了，但**定型不等於什麼都驗過**。2026-08-08 的實話：

| | 狀態 |
|---|---|
| `tooljson` 全部行為 | 45 關離線煙霧測試，每次改都跑 |
| `llms` 的記憶／串流／思考／工具／後設 | 對 DeepSeek 雲端和本機 LM Studio 各跑過一輪五關 |
| `caps` 七項全部 | 兩邊各實打一次才宣告，**不抄 litellm 的內建資料庫** |
| 串流的 `stream_options` usage | DeepSeek 和 LM Studio 都拿得到 |
| `ollama-*` 那批 | 2026-08-12 已直連驗證文字與 Qwen tool calling；完整證據見 [`../../docs/freepy/ollama-192.168.1.146.md`](../../docs/freepy/ollama-192.168.1.146.md) |
| tool JSON 的 wire format | **故意還在 `0.1.0`** —— 只有一個語言的實作，還不知道規則寫得夠不夠死（[FORMAT.md](tooljson/FORMAT.md)） |

實打抓到的一個真錯：litellm 說 DeepSeek `supports_response_schema` 是 `True`，
實際送 `response_format` 回 **400 `This response_format type is unavailable now`**。
yaml 已經覆寫成 `false`。**沒驗過的就寫「沒驗過」，不裝作驗過了** —— 這條比驗過
幾項重要。

## 三個各自的分界

**`llms` 不做的事**：重試、logging、config 檔、CLI、執行工具。它只把一次
API 呼叫變成一個 `Reply`，而且**永遠回 `Reply`，絕不丟例外**。

**`tooljson` 不做的事**：掃資料夾找工具、產 spec。要讀哪幾份 .json 是呼叫端
明講的。反過來，它**該做而且做到了**的是開放 `_type`：要加一種執行方式
（HTTP、python import…）是 `tooljson.register()`，不用改 llmkit 的程式。

**`proxy` 不做的事**：它就是一份 litellm 設定，沒有自己的程式碼。

## 慣例

- **一個檔 150 行以內**，程式和文件都算。超過就拆。
- **一個檔一件事。**
- **沒有 test**，驗證靠 `python -m` 的煙霧測試實跑。
- **工具的回傳值永遠是字串，錯誤也是字串**，因為它會直接變成送回模型的 tool
  message。模型讀得懂錯誤就能自己重試，讀到例外只會整條斷掉。
  對照組是**壞掉的設定檔用丟的** —— 那是人的錯，越早炸越好。
- **不安靜地丟掉東西。** 模型明明有給的參數，不認得就報錯，不要當作沒看到 ——
  litellm 的 `drop_params` 就是這個坑。
- **要宣告一件事之前先實測一次。** `proxy/litellm.yaml` 裡那些能力旗標都是打過才寫
  的，不是抄文件；沒打過的那幾個在 [`llms/TOOLS.md`](llms/TOOLS.md) 裡標明「已知的
  不足」，而不是裝作驗過了。
