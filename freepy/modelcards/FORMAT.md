# card 的格式

一顆模型一份 `cards/<id>.json`，**檔名要跟 `id` 一模一樣**。
現在的版本是 `0.1.0`，`_version` 對不上直接拒收。

```json
{
  "_version": "0.1.0",
  "id": "qwen3.5-9b",
  "weights": "qwen/qwen3.5-9b",
  "runner": "lm_studio",
  "aliases": {"lm-qwen3.5-9b": "think", "lm-qwen3.5-9b-nothink": "nothink"},
  "context": {"v": 262144, "src": 1},
  "modes": {
    "think":   {"temperature": {"v": 1.0, "src": 1}},
    "nothink": {"temperature": {"v": 0.7, "src": 1}}
  },
  "claimed":  {"tools": {"v": true, "src": 1}},
  "verified": {"json_schema": {"v": true, "on": "2026-08-08", "how": "送 response_format 收得下"}},
  "sources": [{"n": 1, "url": "https://…", "kind": "official", "read": "2026-08-09"}],
  "notes": "隨手記，沒有格式"
}
```

必填：`_version` `id` `weights` `runner` `aliases` `modes` `sources`。
選填：`context` `claimed` `verified` `notes`。

## 欄位

**`id`** —— 照**底層權重**取名，不照 litellm alias。alias 是多對一：`-nothink` 是同一份
權重的另一種用法，不是另一顆模型。

**`weights`** —— 那個 runner 認得的完整字串（`qwen/qwen3.5-9b`、`qwen3:32b`、
`deepseek/deepseek-chat`），也就是 `litellm.yaml` 裡 `model:` 去掉 provider prefix 的部分。

**`runner`** —— `deepseek` / `ollama` / `lm_studio`。

**`aliases`** —— `{litellm 的 model_name: mode 名}`。**是 map 不是 list**，因為值要指出
那個名字對應哪一組建議參數。值必須是 `modes` 裡真的有的鍵。

**`context`** —— 官方標的 context window，token 數。runner 的預設值可能更小
（Ollama 常常是 4096），那是兩件事，落差寫進 `notes`。

**`modes`** —— `{mode 名: {參數名: 值節點}}`。mode 名沒有規定，但慣例是
`think` / `nothink` / `default`。只有一種用法的模型就只寫 `default`。

**`notes`** —— 純文字。官方自己講不一致、chat template 的坑、量化版差異，都丟這裡。

## 值節點：`{"v": …, "src": n}`

`modes`、`claimed` 和 `context` 裡**每一個值都是這個形狀**，裸值會被拒收。
`src` 要指到 `sources` 裡存在的 `n`。

出處是這張表唯一的價值。參數不對勁的時候，第一件事是回去看那個值哪來的 ——
裸值等於沒有這個能力。

**查不到就不要有那個鍵。** 不要填 `null`、不要猜、不要從同系列別顆模型推。
空著是有效答案，代表「沒查到」；`false` 代表「查到了，官方說不支援」。
這跟 `llms/caps.py` 用 `None` 表達「proxy 沒說」是同一套規矩。

## `claimed` 和 `verified` 分開，形狀也不同

| | 誰填的 | 形狀 |
|---|---|---|
| `claimed` | 查文件的人 | `{"v": bool, "src": n}` |
| `verified` | 我們自己實打過 | `{"v": bool, "on": "YYYY-MM-DD", "how": "怎麼打的、看到什麼"}` |

`verified` 沒有 `src`，因為**來源就是我們自己**，要記的是哪一天、怎麼打的。
形狀不同是刻意的：貼錯格子會當場被擋下來。

兩邊的鍵都限定這七個，跟 `llms/caps.py` 的 `FIELDS` 一模一樣：

`tools` `tool_choice` `parallel_tools` `vision` `reasoning` `json_schema` `caching`

對齊的用處是 `verified` 填完可以直接生成 `litellm.yaml` 的 `model_info`，
中間不用再翻譯一次 —— 翻譯就會有翻錯的機會。

**只有 `verified` 會被 `caps_for()` 吐出來。** `claimed` 是情報，要它得明講
`trust="claimed"`。理由見 [README.md](README.md)：litellm 的內建資料庫謊報過兩次，
網搜來的東西沒有理由比它可信。

## `sources`

```json
{"n": 1, "url": "https://huggingface.co/Qwen/Qwen3.5-9B", "kind": "official", "read": "2026-08-09"}
```

`n` 從 1 開始不能跳號，`read` 是抓取日期（`YYYY-MM-DD`）。`kind` 三種：

| kind | 是什麼 | 例子 |
|---|---|---|
| `official` | 模型作者自己發的 | 官方 HF model card、官方文件站、官方 repo |
| `runner` | **跑的人**自己報的，不是作者說的 | LM Studio 的 `/api/v0/models`、`ollama show` |
| `secondary` | 以上都找不到才用 | 聚合站的模型頁 |

`runner` 單獨一類，是因為它跟 `official` 的可信度不同方向：LM Studio 說
`max_context_length` 是它**真的會開**的值，但它說的 `capabilities` 只是它自己貼的標籤。
部落格、Reddit、二手整理表、別的 AI 的回答**都不是來源**。

## 參數名照官方寫的抄

不要翻譯成 `llms.Params` 的欄位名。`repetition_penalty` 就寫
`repetition_penalty`，不要改成 `frequency_penalty` —— 那是兩個不同的東西。

`Params` 有欄位的（`temperature` `top_p` `max_tokens` `seed` `stop`
`presence_penalty` `frequency_penalty`）會直接送，其餘一律進 `Params.extra`。
進 extra 的東西**可能被 proxy 的 `drop_params: true` 無聲丟掉**，
`python -m modelcards` 會把它們列出來，之後補進 `allowed_openai_params`。
