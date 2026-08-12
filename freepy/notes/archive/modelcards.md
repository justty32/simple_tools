# modelcards 歷史決策與實測

> 這是被 2026-08-10 preset 簡化決策取代的背景，不描述目前 runtime API。現況見
> [`NOTES.md`](../README.md) 與 [`llmkit/llms/presets.json`](../../llmkit/llms/presets.json)。

## 何時曾考慮搬進 llmkit

當時的判準是 modelcard 介面是否穩定；缺口集中在 mode 層級資料無處存放：

- `reasoning_effort: "none"` 才是 `-nothink` alias 的真正開關，但只焊在 proxy YAML，card
  不是自我完備。
- `tool_choice` 隨 DeepSeek mode 改變：`deepseek-chat` 接受 `"required"`，
  `deepseek-reasoner` 回 400；整張 card 一格 `verified` 無法表達這個差異。

兩者都表示能力與 runner 旋鈕掛在 mode，而不是整顆 model。當時決定先累積更多模型實證，
不急著把未定型介面搬入 llmkit；後來則直接讓整套 modelcards 退場。

## 為什麼 claimed 與 verified 分開

早期 `modelcards/` 將網路研究的建議參數和能力存為 JSON。核心規則是 `claimed` 與 `verified`
分開，`caps_for()` 預設只回傳實打過的 `verified`。LiteLLM 內建資料曾同時出現 false negative
和 false positive，因此網路來源不能直接變成 runtime capability。

兩種資料的形狀也刻意不同：`claimed` 帶 `src`，`verified` 帶 `on`／`how`，降低貼錯欄位的機會。
驗收以 `python -m modelcards` 和 alias 集合對帳執行，不靠人工目視；card 多出不存在 alias 是
錯誤，YAML 多出尚未研究的 alias 只列進度，避免長期紅燈被訓練成忽略。

## LM Studio／Qwen3.5-9B 實打結果

### 非 OpenAI 參數不能攤成頂層 kwarg

`top_k`、`min_p`、`repetition_penalty` 在送出前便被 OpenAI SDK 以 unexpected keyword 拒絕，
必須包進 `extra_body`。原先擔心 LiteLLM `drop_params` 靜默吞掉，實際上更早死在 SDK；包裝後
LM Studio 可以直接接收。

### `-nothink` alias 只在 proxy 上成立

直接呼叫 LM Studio 時，`lm-qwen3.5-9b-nothink` 仍會思考。真正關閉思考的是 proxy preset 的
`reasoning_effort: "none"`；手動補上後 reasoning 長度才歸零。這證明 alias 名稱不能取代
runner parameters。

### 第一手來源也會矛盾

Qwen3.5-9B 官方 README 對 instruct reasoning 提供兩組不同參數：`top_p` 0.95 vs 1.0、
`top_k` 20 vs 40、`presence_penalty` 1.5 vs 2.0。官方 discussion 當時也沒有答案，所以研究
流程必須能記錄衝突，而不是把第一手來源當成天然一致。

LM Studio `/api/v0/models` 比 `/v1/models` 多回傳 context length、量化、type 和 capabilities，
但同樣會自相矛盾：qwen3.5-9b 標成 `vlm`，vision 卻不在 capability list。因此 runner 自報、
官方聲明與本地實測必須分開保存。
