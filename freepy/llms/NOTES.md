# llms — 工作筆記

跨 session 的決定和實測結果。**只記不看 code 看不出來的東西**；已經寫在 README／USAGE 裡的用法不重複抄。

## 能力旗標是實測出來的，不要信 litellm 的內建資料庫

`/model/info` 的答案有兩個來源：`litellm.yaml` 裡手寫的 `model_info`，和 litellm 自己帶的模型資料庫。**後者對雲端模型也會過期**：

- **2026-08-05 實測**：litellm 說 `deepseek-reasoner` 的 `supports_function_calling` 是 `False`，但實際打過去它照樣回 tool_calls。所以 yaml 裡手動覆寫成 `true` —— 不覆寫的話 `client._reject()` 會在本地就把你擋死，根本送不出去。
- 本機模型（ollama、LM Studio）litellm 一律不認得，不宣告就全是 `None`。

所以規矩是：**要宣告某個旗標之前，先實打一次**。yaml 裡那些 true/false 都是這樣來的，不是抄文件。

`supports_reasoning` 跟另外兩個不同，**它不擋任何呼叫**。思考不是你送出去的參數，是模型自己的事，這旗標只是回答「值不值得去讀 `last_reasoning`」。加擋的邏輯進去只會製造假錯誤。

## 思考走的是 reasoning_content 這個欄位

DeepSeek 和 LM Studio（qwen3.5、gemma-4）**都是**把思考放在 `message.reasoning_content` / `delta.reasoning_content`，不是把 `<think>...</think>` 塞在 content 裡。所以答案拿到手就是乾淨的，不用自己剝標籤。litellm proxy 有把這個欄位透傳過去，2026-08-05 對 `lm-gemma-4-e4b` curl 過確認。

思考**不寫回 history**：DeepSeek 這類 API 不收你回傳的 `reasoning_content`，寫回去下一輪就爆。

**混合式思考模型會自己決定要不要想。** `lm-gemma-4-e4b` 連問四次同一題，三次有思考（1000+ 字）、一次完全沒有 —— 沒想的那次回應裡根本沒有 `reasoning_content` 這個 key，`last_reasoning` 就是 `None`。**這是正常的，不是管線斷了**，追這個會浪費很多時間。附帶一提，偷懶不想的那次答案也真的答錯（9.11 > 9.9）。

## 開關思考：統一成「換名字」

**兩家的機制不同，但 yaml 把它們包成同一種用法**，呼叫端只要換 `model` 字串：

- **DeepSeek** 本來就只能換名字（`deepseek-chat` / `deepseek-reasoner` 是同一顆 v4-flash 的兩個模式）。它**不吃 `reasoning_effort`** —— 2026-08-05 實測給 `"none"` 照樣想了 92 字。
- **LM Studio** 吃 `reasoning_effort`（直打 1234 驗過，`"none"` 思考確實消失），但要每次帶參數很煩，所以每顆模型在 yaml 裡多開一個 `-nothink` 分身，把 `reasoning_effort: "none"` 焊死在 `litellm_params` 裡。

**那個會坑死人的細節**：litellm 的支援參數清單裡**沒有** `reasoning_effort`（`/model/info` 的 `supported_openai_params` 可以查），而我們開了 `drop_params: true`，所以它會被**無聲丟掉** —— 不報錯、不警告，你以為關掉了其實沒有。解法是那三個 `lm-*` 都加 `allowed_openai_params: ["reasoning_effort"]` 強制放行。這是 `drop_params: true` 的一般性代價，以後任何「參數送了沒反應」都先往這裡查。

LM Studio 那區的 yaml 用 **YAML anchor（`&lm` / `<<: *lm`）** 收掉重複，順便壓在 150 行以內。`-12b-nothink` 是唯一沒有自己覆寫 `model:` 的一筆，完全靠繼承，改動那區之後要特別驗它有沒有打到正確的模型（看 LM Studio 載入了哪顆）。

試過但**沒用**的兩招，別再繞回去：`chat_template_kwargs: {enable_thinking: false}`（無效，思考照舊）、prompt 尾巴加 `/no_think`（反效果，它會開始思考「什麼是 /no_think」，思考字數反而翻倍）。

## 串流的兩個坑（都是原本 llms.py 漏掉的）

**tool_calls 在串流時是碎片。** `id` 和 `name` 通常只出現在第一片，`arguments` 是一小段一小段接起來的，要**依 `delta.index` 分組**才拼得回去（一次可能有多個 call 交錯）。原本的 `__next__` 只讀 `delta.content`，所以串流下的工具呼叫整包消失。

**工具回合的歷史要連 `tool_calls` 一起寫回去**，形狀要跟 API 收的一模一樣（`{"id", "type": "function", "function": {"name", "arguments"}}`）。少了它，第二輪送 `tool_results` 時 API 會說對不上。2026-08-05 對 deepseek 和 gemma-4-e4b 都跑過完整兩輪。

**疊代只吐答案的字**，思考和工具收在旁邊當屬性。理由：`for ch in handler: print(ch)` 是最常見的用法，把思考混進去會直接印到使用者臉上；而工具碎片要收完才有意義，本來就不適合逐字吐。

## 模型清單（2026-08-05 實測）

**DeepSeek**：`/models` 只列 `deepseek-v4-flash` 和 `deepseek-v4-pro`。`deepseek-chat` / `deepseek-reasoner` 是舊名字，API 仍然收，分別對應 v4-flash 的**非思考**和**思考**模式（送 `deepseek-chat` 回應的 `model` 欄位會顯示 `deepseek-v4-flash`）。三個名字都留在 yaml 裡。

**LM Studio**：model id 用 `curl localhost:1234/v1/models` 問，不要猜 —— 原本 yaml 裡是 `local-model` 這個佔位假值，**從來沒有可能通過**。目前磁碟上有三個，全部實測過：叫得動工具、都會思考、都讀得懂圖（丟一張純紅色 PNG 進去回「紅色」）。

`gemma-4-31B` 載不動（`unable to allocate CUDA0 buffer`，`~/.lmstudio/server-logs` 裡有記錄），所以沒放進 yaml。LM Studio 沒載入模型時第一次呼叫會 JIT 載入，會等一下，`timeout` 要放寬。

## 這台機器（Manjaro）的環境

**系統 python 裝不了東西是兩個原因疊在一起**，別搞混：PEP 668 擋 pip 寫進 pacman 管的 `site-packages`（這是保護，不要用 `--break-system-packages` 繞）；而且系統 python 是 **3.14**，太新，很多套件還沒有 wheel。

所以：

- **python 用 uv 自己管的 3.13**（`~/.local/share/uv/python/`，已經裝好了）。除了 wheel 比較齊，還躲掉 Arch 的經典坑 —— `pacman -Syu` 把系統 python 升版時，建在它上面的 venv 會整個死掉。
- **litellm 不裝進任何 venv**，用 `uv run --with 'litellm[proxy]'` 臨時拉起來（`start_litellm.sh` 就是這樣）。`fastapi` 要釘 `<0.119`，新版跟 litellm[proxy] 的 pydantic 會打架。
- **freepy 自己的 venv 在 `freepy/.venv`**，靠 `freepy/pyproject.toml` 宣告依賴。那份 pyproject 有 `[tool.uv] package = false`，意思是「只裝依賴，別把這資料夾當套件去 build」—— 沒有這行 uv 會要你補 build backend。

## VS Code / 跨到 Windows

- **`python.analysis.extraPaths` 一定要有 `freepy`**。`llms` 是 `freepy/` 底下的 package，開整個 repo 當 workspace 時 Pylance 從 root 找，`from llms import LLM` 會被畫紅線。`launch.json` 的 `cwd` 設成 `freepy` 是同一個原因。
- **直譯器路徑沒辦法分平台寫**（linux 是 `.venv/bin/python`，Windows 是 `.venv\Scripts\python.exe`），Windows 上第一次要自己選一次。選擇存在本機不進版控，所以兩邊各選一次就好。
- **`.gitattributes` 是必要的，不是潔癖**。沒有它，Windows checkout 會把 `.sh` 轉成 CRLF，回到 Linux 執行就是 `bad interpreter: /usr/bin/env bash^M`，第一次遇到會找很久。
- PowerShell 預設擋 `.ps1`，`start_litellm.ps1` 跑不動時是 ExecutionPolicy 的問題，不是腳本錯。

## ask() 裡三個踩過的雷（2026-08-05 修掉）

**送出前就寫歷史，失敗要收回來。** user message 是在呼叫 API 之前就 append 進 `history` 的，早期版本失敗時沒收回去，於是歷史裡會堆一串沒人回答的問題，下次成功時整包送出（有些 API 不收連續兩則 user）。現在 `ask()` 一進來先記 `checkpoint = len(self.history)`，`except` 裡 `del self.history[checkpoint:]`。

**`params.extra` 不能蓋掉 model / messages / stream。** 早期是先建 kwargs 再 `update(params)`，所以 `extra={"stream": True}` 蓋得掉 `stream=False`，但 `if stream:` 判斷用的是函式參數，結果拿串流物件去走非串流的路，噴 `'Stream' object has no attribute 'choices'`。現在順序反過來：params 先展開，這三個最後寫。

**只給圖不給文字要能送。** 判斷式原本是 `if prompt is not None`，於是 `ask(images=[...])` 沒有 prompt 時圖片組好了卻沒被加進 messages，只送出一個空的 messages 陣列。現在是 `if prompt is not None or images`。

還有一個**沒修、刻意留著**的：handler 拿了卻完全不碰（不疊代也不 close），assistant 訊息就不會寫進歷史、連線也不關。在 `__del__` 裡收尾會在 GC 時間點動到 `history`，比問題本身更糟，所以只在文件裡寫清楚用 `with`。

**`oneshot.ask()` 拿掉了**（原本是 module 層的一次性問答）。它只轉送 `model`/`system`/`key`，`timeout`、`caps` 都被安靜吃掉，而它做的事就是 `LLM(...).ask(remember=False)` —— 少一個會騙人的入口比較好。

## 慣例

- **一個檔 150 行以內**，程式和文件都算。超過就拆 —— README 超過就拆出 USAGE.md。
- **沒有 test，驗證靠實跑**：proxy 起著，`cd freepy && uv run python -m llms <一般模型> <思考模型>`，四關（記憶／串流／思考／工具）都要過。碰到串流＋工具、串流＋思考這種組合，`__main__.py` 沒涵蓋，要另外手動打一次。
- 改完 `litellm.yaml` **要重啟 proxy**，程式端還要 `LLM.clear_caps_cache()` —— 能力表是照 proxy 根位址快取的，查不到的空表也算查過，不會自動重試。
