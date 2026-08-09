# freepy — 工作筆記

**這份不是 llmkit 的文件，是工作間的筆記** —— 跨 session 的決定和實測結果。
**只記不看 code 看不出來的東西**，以及「為什麼當初這樣決定」；已經寫在
`llmkit/*/README.md` 裡的用法不重複抄。要給別人看的東西全在
[`llmkit/`](llmkit/README.md)，那邊是 release 狀態。

這台機器怎麼設定的（Manjaro、uv、VS Code、跨到 Windows）在 [ENV.md](ENV.md)。
`llmkit` 定型**之前**踩出來的東西（`Reply` 為什麼長這樣、串流的坑、`ask()` 修過什麼、
模型清單和 caps 怎麼問出來的）已經結案，收在 [NOTES-llmkit.md](NOTES-llmkit.md)。

## 2026-08-09 路線：四層疊上去，順序不能顛倒

要做的是一整套 agent，但**不是直接開一個檔案寫 agent**，是四層疊上去，
每一層自己就能用、也自己驗得完：

1. **proxy 開好** —— 所有模型統一成 `litellm.yaml` 的 alias，上面幾層只認 alias，
   不知道背後是雲端、遠端 ollama 還是本機 LM Studio。
2. **`llms` ＋ `modelcards` ＝ 基礎 bot 模型** —— `engine_for(alias)` 一行把建議參數
   和實打過的能力套上去，`LLM` 負責一輪一個 `Reply`。**這層只會講話，不會做事。**
3. **`tooljson` 給它能力** —— 工具是一份 JSON 而不是一段程式碼，所以「有哪些能力」
   變成設定檔的事。到這裡 bot 會做事了，但每一輪還是人在推。
4. **在這之上才是 agent** —— 自己決定推幾輪、什麼時候收手。

順序不能顛倒的理由是**下面一層錯了，上面一層的症狀會完全看不出來源**：模型能力宣告
錯（第 2 層）會表現成 agent 莫名其妙不叫工具（第 4 層）。這就是為什麼 caps 要實打、
為什麼 `modelcards` 存在 —— 它是第 2 層的地基，不是附加功能。

### modelcards 什麼時候能進 llmkit

判準是「介面還會不會變」。現在還會，而且缺口很具體：**mode 層級的東西記不進卡。**
已經撞到兩次：

- `reasoning_effort: "none"` 才是 `-nothink` alias 真正的開關，焊在 yaml 的
  `litellm_params` 裡，卡上沒有 —— 卡不自我完備，不經 proxy 就不對。
- `tool_choice` 在 DeepSeek 是**跟著 mode 走**的：`deepseek-chat` 收 `"required"`，
  `deepseek-reasoner` 直接 400。但 `verified` 是整張卡一格，記不下這個差別，
  所以那張卡的 `tool_choice` 只好空著 —— 明明打過了卻沒地方寫。

兩個是同一個形狀：**能力和 runner 旋鈕其實掛在 mode 上，卡卻只有整卡一格。**
一個例子不夠定規則，兩個開始像了。ollama 那四顆實打完再決定要不要把 `verified`
下放到 mode，**在那之前不搬進 llmkit** —— 搬進去就等於宣告介面定了。

## 2026-08-09 modelcards：為什麼查來的和打過的要分兩格

新的一包 [`modelcards/`](modelcards/README.md)，把每顆模型的建議參數和能力記成 JSON，
資料由另一個 agent 上網查（任務書 `RESEARCH.md`）。**核心決定是 `claimed` 和
`verified` 分開存，而且 `caps_for()` 預設只吐 `verified`。** litellm 的內建資料庫已經
謊報過兩次（一次成 False、一次成 True），網搜來的東西沒有理由比它可信 —— 所以
「先實打再宣告」這條規矩這次是**寫進程式**的，不是只寫在文件裡。兩者連形狀都不同
（`claimed` 有 `src`，`verified` 有 `on`／`how`），貼錯格子會當場被擋。

驗收改成可執行的：`python -m modelcards` 全綠 + 進度 13/13，而不是人工讀一遍。
其中一關拿 alias 集合去跟 `litellm.yaml` 對帳 —— 卡多出來的算錯（打錯字），
yaml 多出來的只印進度不算失敗，**一直紅的關卡會被訓練成無視**。

### 實打抓到兩個（LM Studio，qwen3.5-9b）

**非 OpenAI 的參數不能攤成頂層 kwarg。** `top_k` / `min_p` / `repetition_penalty`
送出去之前就被 openai SDK 擋掉（`Completions.create() got an unexpected keyword
argument 'top_k'`），要包進 `extra_body`。原本以為這類參數的風險是「被 litellm 的
drop_params 無聲吞掉」，結果**更早、更大聲**地死在 SDK。包好之後 LM Studio 直收。

**`-nothink` 那組 alias 只有經過 proxy 才成立。** 直接打 LM Studio 時
`lm-qwen3.5-9b-nothink` 照樣思考 —— 關掉靠的是 `reasoning_effort: "none"`，
焊在 `litellm.yaml` 的 `litellm_params` 裡，卡上沒有。手動補上就 think 長度 0。
先不改（卡該不該記 runner 的旋鈕還沒想清楚），等第二顆思考模型填進來再看。

### 順帶

Qwen3.5-9B 的官方 README **自己跟自己打架**：instruct 模式「推理任務」那組參數在兩個
章節裡不一樣（`top_p` 0.95 vs 1.0、`top_k` 20 vs 40、`presence_penalty` 1.5 vs 2.0），
有人開 discussion 問了沒人回。這正是 RESEARCH.md 要求回報「矛盾」的理由 ——
第一手來源也會前後不一致，不是抄下來就沒事。

LM Studio 的 `/api/v0/models`（不是 `/v1/models`）會多給 `max_context_length`、
量化、`type: vlm`、`capabilities`，查本機模型比猜快得多。但它自己也不一致：
qwen3.5-9b 標成 `vlm` 卻沒把 vision 列進 `capabilities`。所以 card 的來源分三類，
`runner`（跑的人自報）跟 `official`（作者說的）不混為一談。

## 2026-08-08 llmkit 成形：為什麼是這三塊

`llms` / `proxy` / `tooljson` 從 freepy 裡抽出來變成地基，freepy 其餘的東西
（`base_tools`、`exec_tools` 的掃描、`prototypes`）長在它上面。

界線是「**介面還會不會變**」，不是「寫完了沒」。`base_tools` 的 `read_file` 早就會跑，
但它的路徑規則還在動，所以留在上面那層；`tooljson` 的格式一旦有第二個語言的實作
就改不動了，所以它得先定型。

`proxy` 沒有程式碼卻自成一塊，是因為那些 yaml 裡的 true/false **是實測結論**，
不是設定 —— 它的價值在 `README.md` 而不在 `litellm.yaml`。

`tooljson` 的 `_type` 改成註冊表也是這時候做的。原本 `TYPES = ("exec",)` 寫死，
第三方要加一種執行方式就得改 llmkit 的程式 —— 那樣「規範」就退化成「某個 lib 的
設定檔格式」了。連帶把執行路徑從 `invoke.run()` 改成 `Spec.run()` → `body.run()`，
不然登記得進去也跑不起來。

## 慣例

- **一個檔 150 行以內**，程式和文件都算。超過就拆 —— README 超過就拆出 USAGE.md。
- **沒有 test，驗證靠實跑**：`cd freepy/llmkit && uv run python -m tooljson`（離線，
  27 關）、`cd freepy && uv run python -m modelcards`（離線，31 關）和
  `uv run python -m llms <一般模型> <思考模型>`（要 proxy，五關）。
  串流＋工具、串流＋思考這種組合 `__main__.py` 沒涵蓋，要另外手動打一次
  （`try.py` 就是串流＋工具）。
- **不連 proxy 也驗得動 `Reply`**：拿假的回應物件餵 `Reply(...)`，串流和非串流兩條路
  應該吐出一模一樣的 `text` / `calls` / `history`。改 `reply.py` 前先這樣掃一遍比較快。
