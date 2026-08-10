# freepy — 工作筆記

**這份不是 llmkit 的文件，是工作間的筆記** —— 跨 session 的決定和實測結果。
**只記不看 code 看不出來的東西**，以及「為什麼當初這樣決定」；已經寫在
`llmkit/*/README.md` 裡的用法不重複抄。要給別人看的東西全在
[`llmkit/`](llmkit/README.md)，那邊是 release 狀態。

還沒決定要不要做的想法丟在 [IDEAS.md](IDEAS.md)，那邊是收件匣，這邊是決定。
這台機器怎麼設定的（Manjaro、uv、VS Code、跨到 Windows）在 [ENV.md](ENV.md)。
`llmkit` 定型**之前**踩出來的東西（`Reply` 為什麼長這樣、串流的坑、`ask()` 修過什麼、
模型清單和 caps 怎麼問出來的）已經結案，收在 [NOTES-llmkit.md](NOTES-llmkit.md)。

## 2026-08-09 第四層落地：`agentloop`

四層疊完了。[`agentloop`](agentloop/README.md) 只有一個函式 `run()` 和一個把手
`Handle`，但形狀是想過的：

**`run()` 是 async 的，把手是同步的。** 兩件會擋住的事（模型那次 HTTP、工具本體）
丟進 `asyncio.to_thread`，所以 event loop 不會被卡住；把手上的 `now()` / `say()` /
`pause()` 全是普通函式，別的 coroutine、別的 thread、REPL 都問得動、按得動。
這是「放著跑 + 另一條 routine 盯著」那個用法唯一撐得住的組合。

**沒有「立刻中斷」。** 指令一律下一輪開頭生效。理由是 HTTP 和跑到一半的工具本來就
停不下來，硬做出一個 `cancel()` 只會讓人以為工具沒跑過 —— 那比等它跑完危險。

### 一個一開始寫錯、被測試抓出來的順序

原本是「這一輪跑完工具 → 存著結果 → 下一輪送出去」，工具跑在迴圈**尾巴**。
預算就是在這種時候用完的：工具跑掉了（副作用發生了），結果卻沒人送得出去。
再叫一次 `run()` 就會**再跑一遍**。

改成每一輪開頭先還上一輪欠的債，尾巴只記「欠什麼」。這樣預算用完時最後那批工具
根本還沒跑，債留在 `bot.history` 上，同一個 bot 再叫一次就接著跑 ——
**「接著跑」不是另外寫的功能，是把順序排對之後自己掉出來的。**

### 限制分兩種擋法，這條想了最久

- **預算真的沒了** → 停整個 agent（輪數、時間、token、總呼叫數、引擎）
- **只是這支工具不能用** → **回一句話給模型**（不在白名單、單一工具用滿）

第二種故意不是錯誤而是情報：模型讀到「run_shell 你已經用滿 5 次了」會換方法繼續
做事，整條停掉的話它連想都沒機會想。這跟「工具的回傳值永遠是字串」是同一條規矩
往上長一層。

機器資源（cpu / gpu / 記憶體 / 網路 / 檔案）**沒做**，而且刻意不放進 `Limits`
—— 規劃在 [agentloop/LIMITS.md](agentloop/LIMITS.md)。設得下去卻沒人擋的欄位比沒有
更糟：**一個以為自己被關住的 agent，比一個沒關的危險。**

### `quiet`：「不再叫工具就結束」原來是個特例

「連續無工具呼叫次數上限」這條限制逼出一件事：迴圈本來的收手條件（一不叫工具就停）
其實是 `quiet=1`。調大就會推它一把再給幾次機會 —— 也就是
[prototypes](prototypes/README.md) 第 2 輪那個「小模型直接講一段話不動手」的解法。
代價是它**真的**講完的那次也會被多推幾下，白燒幾輪，所以預設維持 1。

### 驗證：假的回應物件餵真的 `Reply`

35 關全離線。`FakeBot` 繼承真的 `LLM`，只是 `ask()` 照劇本吐 `Reply(假 response)`
—— 所以 `history`、`pending_calls`、`calls` 全是真的那條路。NOTES 慣例那條
「不連 proxy 也驗得動 `Reply`」原本是給 `reply.py` 用的，這次整包迴圈都靠它。

## 2026-08-10 多 agent 規劃拆層

原本 [`team_tools/PLAN.md`](team_tools/PLAN.md) 實際只有 mailbox，名稱會讓人以為它已經
處理組織與授權。現在拆成：

- [`communication_tools`](communication_tools/PLAN.md)：直接傳訊、原子 mailbox；
  不知道上下級與任務，也不做阻塞式 `wait_for_message`。
- [`team_tools`](team_tools/PLAN.md)：建在 communication/runtime 上的組織樹、grant、
  allocation、task/report。agent 用 `/root/leader/worker` 形式的 canonical path；
  path 表示組織位置，但不自動等於權限。
- [`agent_runtime`](agent_runtime/PLAN.md)：spawn/fork/lifecycle。child 權限只能縮小，
  consumable budget 必須 reserve，受限 agent 不拿 Podman socket。
- [`memory_tools`](memory_tools/PLAN.md)：無損卸載 tool result，同一 resolver 支援 JSON
  `$ref` 與 Markdown link；ref 不是授權。
- [`introspection_tools`](introspection_tools/PLAN.md)：唯讀回答自己在哪、能做什麼、
  預算還剩多少。

[`agentloop/TURNS.md`](agentloop/TURNS.md) 固定兩層時間單位：一次 `ask() → message` 是
一輪（Round）；模型從指令啟動、經過多輪與工具、最後主動停止是一回合（Turn）。工具
向使用者要輸入仍是該 tool call 的內部事件，不另開一輪。

機器資源的原規劃仍在 [`agentloop/LIMITS.md`](agentloop/LIMITS.md)：rlimit 只管得到
子行程，網路和檔案要靠 namespace／容器，GPU 只容易限制可見裝置，不能承諾 per-process
顯存硬上限。

## 2026-08-09 base_tools 接進 tooljson，順手抓到 `from_tool()` 的一個安靜錯誤

`base_tools/tools.json` 是 `specs.py` 產的，四個工具變成 `_type: "python"` 的 spec。
能力從「import 清單」變成「設定檔」之後，才混得進 exec 型的外部工具。

產的時候撞到：**`python -m base_tools.specs` 的 `__module__` 是 `"__main__"`，
`from_tool()` 從檔名反推成 `"specs"` —— 錯的，應該是 `"base_tools.specs"`。**
而且錯得很安靜：.json 存得出來，等別的行程去讀才 import 不到。

`tool.py` 自己的 docstring 寫著「反推不出來就丟，**不要猜一個**」，它這裡卻猜了一個
還猜錯。修法是一路往上收有 `__init__.py` 的資料夾（`_dotted_name()`）。
順帶：`_source_file()` 問不到檔案時回空字串，`os.path.abspath("")` 是 cwd，
會反推出一個假名字 —— 一起擋掉了。

**這是這個 repo 第二次靠「檔案自己的 docstring」抓到 bug**（上一次是 `invoke.py`
那句「這個檔是 exec 專屬的」）。把「為什麼這樣做」寫進程式裡，會在別人違反它的時候
自己叫出來。

## 2026-08-09 tooljson 加第二種 `_type`：外殼一行都沒動

`_type: "python"` 進來，`spec.py`／`registry.py`／`bind()` 一行都沒改，`_version`
也還是 `0.1.0`。這是 2026-08-08 那個「外殼只讀兩個保留鍵、其餘整包轉交」的設計
**事後被驗了一次** —— 當時挑保留鍵的判準是「所有 `_type` 都成立的東西才配」，
如果那條切錯，第二種 `_type` 進來就會逼著動外殼或升版本。它沒有。

唯一動到的既有檔案是 `invoke.py`，而且是**往外搬不是往內加**：`MAX_OUTPUT`／`clip`
／`decode` 移進 `text.py`。值得記的是它怎麼被發現的 —— `invoke.py` 的 docstring
自己寫著「這個檔是 exec 專屬的」，第二種 `_type` 要用那三個就只能跨界拿或複製一份
常數，兩個都跟那句話牴觸。**是那句 docstring 把它抓出來的**，不是寫的時候想到的。

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
   （2026-08-09 落地成 [`agentloop`](agentloop/README.md)）

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
  45 關）、`cd freepy && uv run python -m modelcards`（離線，31 關）、
  `PYTHONPATH=llmkit uv run python -m base_tools`（離線，26 關）、
  `PYTHONPATH=llmkit uv run python -m agentloop`（離線，35 關）和
  `uv run python -m llms <一般模型> <思考模型>`（要 proxy，五關）。
  串流＋工具、串流＋思考這種組合 `__main__.py` 沒涵蓋，要另外手動打一次
  （`try.py` 就是串流＋工具）。
- **不連 proxy 也驗得動 `Reply`**：拿假的回應物件餵 `Reply(...)`，串流和非串流兩條路
  應該吐出一模一樣的 `text` / `calls` / `history`。改 `reply.py` 前先這樣掃一遍比較快。
