# 任務書：把九顆模型的 card 填完

**這份是要整份拿給另一個 agent 執行的。** 前置作業（格式、lib、驗證關卡、樣本卡）
做完才發車 —— 沒有樣本卡就沒有範本，形狀一定會歪。

## 你要做的事

上網查每顆模型的**官方建議參數**和**官方宣稱的能力**，填成 `cards/<id>.json`。
欄位規範看 [FORMAT.md](FORMAT.md)，形狀照 `cards/qwen3.5-9b.json`（已經填好的樣本）。

**你只寫 `cards/*.json`，不寫程式，也不改任何既有檔案。**

## 九顆

`id` 就是檔名。`aliases` 那欄的值是 mode，要跟 `modes` 裡的鍵對上。

| id | 底層權重 | runner | aliases |
|---|---|---|---|
| `deepseek-v4-flash` | `deepseek/deepseek-chat` | deepseek | `deepseek-chat`→nothink、`deepseek-reasoner`→think |
| `deepseek-v4-pro` | `deepseek/deepseek-v4-pro` | deepseek | `deepseek-v4-pro`→think |
| `qwen3-32b` | `qwen3:32b` | ollama | `ollama-qwen3-32b`→think |
| `qwen2.5-14b-instruct` | `qwen2.5:14b-instruct-q4_K_M` | ollama | `ollama-qwen2.5-14b`→default |
| `deepseek-r1-8b` | `deepseek-r1:8b` | ollama | `ollama-deepseek-r1-8b`→think |
| `gemma3-1b` | `gemma3:1b` | ollama | `ollama-gemma3-1b`→default |
| `gemma-4-12b-qat` | `google/gemma-4-12b-qat` | lm_studio | `lm-gemma-4-12b`→think、`lm-gemma-4-12b-nothink`→nothink |
| `gemma-4-e4b` | `google/gemma-4-e4b` | lm_studio | `lm-gemma-4-e4b`→think、`lm-gemma-4-e4b-nothink`→nothink |
| `qwen3.5-9b` | `qwen/qwen3.5-9b` | lm_studio | **樣本，已經填好，別動** |

`deepseek-chat` 和 `deepseek-reasoner` 是同一顆 `deepseek-v4-flash` 的兩種模式，
所以是**一張卡兩個 alias**，不是兩張卡。

## 來源規矩

**只採信第一手**：官方 model card（HuggingFace／ModelScope）、官方文件站、
官方 GitHub README、官方部落格。這些 `kind` 填 `"official"`。

找不到第一手才退而求其次（Ollama library 頁面的 template／parameters、
LM Studio 的模型頁），`kind` 填 `"secondary"`，並在 `notes` 說明為什麼沒有第一手。
**部落格文章、Reddit、二手整理表、其他 AI 的回答一律不算來源。**

三條硬規矩：

1. **每個值都要有 `src`**，指到 `sources` 裡的編號。驗證程式會擋沒有 src 的值。
2. **查不到就不要寫那個鍵。** 不要填 `null`、不要猜、不要從同系列的別顆模型推。
   空著代表「沒查到」，是有效答案；猜錯會變成之後花好幾小時追的假線索。
3. **版本要對上。** `gemma-4-12b-qat` 是 QAT 量化版，它的官方頁可能跟原版
   `gemma-4-12b` 給不一樣的建議；`qwen2.5:14b-instruct-q4_K_M` 的量化不影響建議參數，
   但要確認你看的是 **instruct** 版不是 base 版。對不上就在 `notes` 寫清楚你看的是哪個。

## 填什麼

**`modes` —— 官方建議的取樣參數。** 常見的是 `temperature`、`top_p`、`top_k`、
`min_p`、`repetition_penalty`、建議的 `max_tokens`／輸出長度上限。官方寫幾個就填幾個，
沒寫的不要補。

思考型模型幾乎都會給兩組（思考／不思考），分別填進 `think` 和 `nothink`。
只有一種模式的模型只寫 `default`。若官方是按**用途**給多組（DeepSeek 的文件就是這樣，
寫程式／翻譯／一般對話各一組），取**一般對話**那組進 `modes`，其餘整段抄進 `notes`。

**`context` —— 官方標示的 context window**，填 token 數。注意 runner 的預設可能更小
（Ollama 預設常常是 4096，跟權重本身的上限是兩回事），這種落差寫進 `notes`。

**`claimed` —— 官方宣稱的能力**，七個鍵，對齊 `llms/caps.py`：

| 鍵 | 官方文件裡通常怎麼講 |
|---|---|
| `tools` | function calling / tool use 支援 |
| `tool_choice` | 能不能強制它一定要叫工具 |
| `parallel_tools` | 一輪能不能吐多個 call |
| `vision` | 吃不吃圖片輸入（多模態） |
| `reasoning` | 有沒有思考／reasoning 模式 |
| `json_schema` | structured output / JSON schema 約束 |
| `caching` | prompt caching / context caching |

七個很少會全部查得到，**查得到幾個填幾個**。這格是情報不是結論 —— 我們之後會自己
實打一次才敢宣告，所以你填的東西不會直接被信，但填錯會害我們打錯方向。

**`notes` —— 值得記但沒有欄位的東西**：chat template 的注意事項、官方特別警告不要
設某個參數、思考模式怎麼開關、system prompt 的建議寫法、量化版的差異。

## 不准做的事

- **不改** `litellm.yaml`、任何 `.py`、任何既有的 `.md`。你的 diff 應該只有
  `cards/*.json` 新增八份。
- **不呼叫模型、不打 API、不起 proxy。** 這一輪不花任何 token，也不需要網路以外的東西。
- **不要為了填滿而填。** 空欄位是這份工作的正確產物之一。

## 驗收

```bash
cd freepy && uv run python -m modelcards
```

兩個條件都要滿足：最後一行 **`N/N 過`**（沒有 FAIL），而且最下面的
**進度是 `13/13 個 alias 有卡`**（沒有「還沒填」那行）。

它會檢查形狀合法、每個值有 `src`、`sources` 編號沒斷、alias 不重複，
以及卡上的 alias 都真的在 `proxy/litellm.yaml` 裡（打錯字會在這關現形）。

順手看一眼 `uv run python -m modelcards table`，那是從你填的 JSON 生出來的，
一眼就看得出哪顆的欄位特別空。

然後交一份簡短回報，**只講三件事**：

1. 哪幾顆的哪些欄位**查不到**，以及你找過哪裡。
2. 哪幾顆只有二手來源。
3. 過程中發現的**矛盾**：官方兩個地方講得不一樣、或官方講的跟
   `proxy/litellm.yaml` 現在的宣告相反。第三點最有價值 —— 那是我們下一輪要優先實打的。
