# freepy — 工作筆記

**這份不是 llmkit 的文件，是工作間的筆記** —— 跨 session 的決定和實測結果。
**只記不看 code 看不出來的東西**，以及「為什麼當初這樣決定」；已經寫在
`llmkit/*/README.md` 裡的用法不重複抄。要給別人看的東西全在
[`llmkit/`](llmkit/README.md)，那邊是 release 狀態。

這台機器怎麼設定的（Manjaro、uv、VS Code、跨到 Windows）在 [ENV.md](ENV.md)。

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

## 2026-08-08 改成 bot 模型：為什麼回傳值只剩一個 Reply

原本 `ask()` 的 result 會是三種型別（`StreamHandler` / calls list / str），呼叫端得
`isinstance` 分辨。改掉的**直接理由是它在漏東西**：模型完全可以一邊說話一邊叫工具
（「好，我幫你查一下」＋ `get_weather(...)`），舊的 `if msg.tool_calls:` 那條路只回
calls，那句話進了歷史但呼叫端永遠看不到，畫面上就是一片空白然後工具突然跑起來。
串流那條反而沒這問題（`StreamHandler` 本來就同時有 `.text` 和 `.tool_calls`），
所以是**把非串流併過去**，不是發明新東西。

**`(result, err)` 收進 `reply.err` 也是收拾既有的不一致**，不是新慣例：串流的錯誤
本來就不可能在 tuple 裡（錯誤是 `ask()` 回來之後才發生的），`StreamHandler.err`
早就存在，等於 err 有兩個住處。`bool(reply)` 出錯時是 `False`，補回 tuple 那種
「逼你看一眼 err」的效果。工具函式**維持回傳字串**，理由沒變（回傳值會直接變成
送回模型的 tool message）。

非串流的 `Reply` 建構時就跑完 `_absorb()` + `_finish()`，所以歷史寫入的時間點跟舊版
一樣（`ask()` 回來就寫好了）。只有串流是延後到收尾才寫，這點也沒變。

### 改完自己審一遍，抓到兩個

**「絕不丟例外」原本只對非串流成立。** `_pump()` 舊版只把 `next(response)` 包在 try
裡，拆 chunk 的那幾行（`chunk.choices[0].delta.content`）是裸的。後端回了預期外的
形狀就會直接炸到疊代的呼叫端 —— 而那正是最容易出事的路徑（本機模型經 litellm 透傳）。
根本原因是**串流的錯誤發生在 `ask()` 回來之後，`ask()` 的 try 管不到**，所以 `Reply`
自己得再包一層。現在拆 chunk 整段都在 try 裡，另外欄位一律用 `getattr` 取，
`delta` 是 `None` 這種還能撐過去不算錯。

**串流一個字都沒收到就斷線，會在記憶裡留下沒人回答的問題。** 非串流那條有
`del history[checkpoint:]` 收回來，串流沒有（同樣是因為錯誤發生得太晚）。留著的後果
是下次再問就變成連續兩則 user message，有些 API 不收。現在 `Reply` 收 `checkpoint`，
整輪落空時自己退回去。判斷「有沒有講過話」用 `finish_reason is None` —— 正常講完但
內容是空的不算落空，這樣才分得開「模型沒話說」和「話還沒開始就斷了」。

## 串流的兩個坑（都是原本 llms.py 漏掉的）

**tool_calls 在串流時是碎片。** `id` 和 `name` 通常只出現在第一片，`arguments` 是一小段
一小段接起來的，要**依 `delta.index` 分組**才拼得回去（一次可能有多個 call 交錯）。
原本的 `__next__` 只讀 `delta.content`，所以串流下的工具呼叫整包消失。

**工具回合的歷史要連 `tool_calls` 一起寫回去**，形狀要跟 API 收的一模一樣
（`{"id", "type": "function", "function": {"name", "arguments"}}`）。少了它，第二輪送
`tool_results` 時 API 會說對不上。2026-08-05 對 deepseek 和 gemma-4-e4b 都跑過完整兩輪。

**疊代只吐答案的字**，思考和工具收在旁邊當屬性。理由：`for ch in handler: print(ch)`
是最常見的用法，把思考混進去會直接印到使用者臉上；而工具碎片要收完才有意義。

## ask() 裡三個踩過的雷（2026-08-05 修掉）

**送出前就寫歷史，失敗要收回來。** user message 是在呼叫 API 之前就 append 進
`history` 的，早期版本失敗時沒收回去，於是歷史裡會堆一串沒人回答的問題，下次成功時
整包送出（有些 API 不收連續兩則 user）。現在 `ask()` 一進來先記
`checkpoint = len(self.history)`，`except` 裡 `del self.history[checkpoint:]`。

**`params.extra` 不能蓋掉 model / messages / stream。** 早期是先建 kwargs 再
`update(params)`，所以 `extra={"stream": True}` 蓋得掉 `stream=False`，但 `if stream:`
判斷用的是函式參數，結果拿串流物件去走非串流的路，噴
`'Stream' object has no attribute 'choices'`。現在順序反過來：params 先展開，
這三個最後寫。

**只給圖不給文字要能送。** 判斷式原本是 `if prompt is not None`，於是
`ask(images=[...])` 沒有 prompt 時圖片組好了卻沒被加進 messages，只送出一個空的
messages 陣列。現在是 `if prompt is not None or images`。

還有一個**沒修、刻意留著**的：handler 拿了卻完全不碰（不疊代也不 close），assistant
訊息就不會寫進歷史、連線也不關。在 `__del__` 裡收尾會在 GC 時間點動到 `history`，
比問題本身更糟，所以只在文件裡寫清楚用 `with`。

**`oneshot.ask()` 拿掉了**（原本是 module 層的一次性問答）。它只轉送
`model`/`system`/`key`，`timeout`、`caps` 都被安靜吃掉，而它做的事就是
`LLM(...).ask(remember=False)` —— 少一個會騙人的入口比較好。

## 模型清單怎麼查出來的（2026-08-05 實測）

**DeepSeek**：`/models` 只列 `deepseek-v4-flash` 和 `deepseek-v4-pro`。
`deepseek-chat` / `deepseek-reasoner` 是舊名字，API 仍然收，分別對應 v4-flash 的
**非思考**和**思考**模式（送 `deepseek-chat`，回應的 `model` 欄位會顯示
`deepseek-v4-flash`）。三個名字都留在 yaml 裡。

**LM Studio**：model id 用 `curl localhost:1234/v1/models` 問，不要猜 ——
原本 yaml 裡是 `local-model` 這個佔位假值，**從來沒有可能通過**。目前磁碟上有三個，
全部實測過：叫得動工具、都會思考、都讀得懂圖（丟一張純紅色 PNG 進去回「紅色」）。

`gemma-4-31B` 載不動（`unable to allocate CUDA0 buffer`，`~/.lmstudio/server-logs`
裡有記錄），所以沒放進 yaml。

**litellm 對 `deepseek-reasoner` 謊報**：它說 `supports_function_calling` 是 `False`，
實際打過去照樣回 tool_calls。所以 yaml 手動覆寫成 `true` —— 不覆寫的話
`client._reject()` 會在本地就把你擋死。結論寫進 `llmkit/proxy/README.md` 了。

## 2026-08-08 把剩下四個 caps 欄位打完

`tool_choice` / `parallel_tools` / `json_schema` / `caching` 原本是照 litellm 的欄位名
抄的，沒實打過。LM Studio 開起來之後補打了 `deepseek-chat` 和 `lm-gemma-4-e4b`：

- **`tool_choice`**：兩邊 `"required"` 都真的逼得出呼叫。
- **`parallel_function_calling`**：兩邊一輪都吐得出 2 個 call（system 要明講「一次全部
  叫出來」，不然模型傾向一次問一個）。litellm 對兩邊都回報 `None`。
- **`response_schema`**：LM Studio 收；**DeepSeek 回 400 `This response_format type is
  unavailable now`**，而 litellm 內建資料庫說它 `True`。**這是第二個謊報，而且方向
  相反** —— 上一個是謊報成 False，這個是謊報成 True。兩個方向都會錯，所以「宣告前
  先實打」不是潔癖。
- **`prompt_caching`**：DeepSeek 真的有（重複前綴第二次 `usage.cached=512`）；
  LM Studio 一直是 `None`，宣告成 false。

順帶驗掉的：**串流的 `stream_options={"include_usage": True}` LM Studio 吃**，
拿得到完整 usage。原本只在 DeepSeek 驗過。

`ollama-*` 那批這次沒驗，192.168.1.146 連不到。**維持不宣告** —— 沒打過就不要寫，
寫了就變成下一個要花時間追的謊報。

驗證用的腳本沒留（一次性的），做法是：`/model/info` 先讀出「proxy 宣稱什麼」，
再對同一顆模型實際送出對應的請求，兩者對照。要重驗照這個路子再寫一支就好。

## 慣例

- **一個檔 150 行以內**，程式和文件都算。超過就拆 —— README 超過就拆出 USAGE.md。
- **沒有 test，驗證靠實跑**：`cd freepy/llmkit && uv run python -m tooljson`（離線，
  27 關）和 `uv run python -m llms <一般模型> <思考模型>`（要 proxy，五關）。
  串流＋工具、串流＋思考這種組合 `__main__.py` 沒涵蓋，要另外手動打一次
  （`try.py` 就是串流＋工具）。
- **不連 proxy 也驗得動 `Reply`**：拿假的回應物件餵 `Reply(...)`，串流和非串流兩條路
  應該吐出一模一樣的 `text` / `calls` / `history`。改 `reply.py` 前先這樣掃一遍比較快。
