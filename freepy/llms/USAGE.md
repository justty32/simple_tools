# 用法

先看 [README.md](README.md) 把 proxy 跑起來，那邊也有 `Reply` 的欄位表。
這份講怎麼跟 bot 講話：串流、思考、圖片、後設。工具和能力在 [TOOLS.md](TOOLS.md)。

共通約定：`ask()` 永遠回傳一個 `Reply`，**絕不丟例外**，錯誤在 `reply.err`，
`bool(reply)` 是 `False`。

## 串流

`stream=True` 拿到的還是同一個 `Reply`，只是它是邊收邊填的。疊代它一次吐一個字，
只吐「答案」的字：

```python
reply = bot.ask("寫首詩", stream=True)
for ch in reply:
    print(ch, end="", flush=True)
print(reply.err)          # 串流中途爆掉的話在這裡，疊代本身不會丟例外
```

`reply.text` / `reply.calls` / `reply.usage` 都會先把剩下的串流收完再給你。
`with` 或 `close()` 可以提前收工，已經收到的部分照樣寫回記憶。

**要看模型邊想邊印就用 `parts()`**，思考和答案都即時，`kind` 是 `"think"` 或 `"answer"`：

```python
for kind, ch in reply.parts():
    print(ch, end="", flush=True)
```

兩種疊代方式**擇一，不要混用**：一般疊代會把路過的思考字元丟掉（完整思考仍留在
`reply.reasoning`），`parts()` 則兩種都給。思考是在答案之前傳完的，所以用一般疊代時
模型想多久畫面就靜止多久 —— 那是正常的，不是當掉。

串流的 `Reply` 拿了卻**完全不碰**（不疊代也不 `close()`）就沒人收尾：這一輪的
assistant 訊息不會進記憶，連線也不會關。不確定會不會讀完就用 `with`。

**一個字都還沒收到就斷線（或被提前 `close()`）的話，這一輪會整個從記憶裡退掉** ——
連你剛剛送出去的那句話一起。理由是不退的話記憶裡會留下一則沒人回答的 user message，
下次再問就變成連續兩則 user，有些 API 直接不收。已經講出幾個字才斷的就留著半截，
那是真的說過的話。

判斷「有沒有講過話」看的是 `finish_reason`：正常講完但內容是空的（`"stop"` 加空字串）
不算落空，不會退。

## 思考

思考內容不會混進答案裡，串流與否都放在 `reply.reasoning`，沒想就是 `None`：

```python
reply = bot.ask("9.11 和 9.9 哪個大？")
print(reply.text, reply.reasoning)
```

思考**不寫回記憶** —— DeepSeek 這類 API 不收回傳的 `reasoning_content`。
所以 bot 記得自己說過什麼、做過什麼，但不記得自己當時為什麼那樣想。

DeepSeek 和 LM Studio（qwen3.5、gemma-4）都是走 `reasoning_content` 這個欄位，
不是把 `<think>` 塞在答案裡，所以答案拿到手就是乾淨的。

### 要它想 / 不要它想

**一律換模型名字**，兩家都一樣。模型是引擎的欄位，直接改：

```python
bot.engine.model = "deepseek-reasoner"       # 這之後都會想
bot.engine.model = "lm-gemma-4-e4b-nothink"  # 這之後都不想
```

改引擎不動人格也不動記憶，同一段對話會用新模型接著講。整顆換掉用
`bot.set_engine(Engine(model=..., timeout=300))`。

底層其實是兩回事，只是被 `litellm.yaml` 包成同一種用法：DeepSeek 的 chat / reasoner
是同一顆 v4-flash 的兩個模式，本來就只能靠名字切；LM Studio 那邊三顆各有一個
`-nothink` 分身，同一顆模型，差別只是設定裡焊死了 `reasoning_effort: none`。

要臨時調思考的多寡（而不是全關），LM Studio 的可以直接送參數：

```python
bot.engine.params = Params(extra={"reasoning_effort": "high"})
```

DeepSeek 不吃 `reasoning_effort`，給了也沒用，只能換名字。

注意 `reasoning` 能力是 `True` 只表示「這模型會思考」，**不表示每次都思考**。
gemma-4-e4b 這種混合式的模型自己決定要不要想，不想的時候回應裡根本沒有
`reasoning_content` 欄位，`reply.reasoning` 就是 `None` —— 這是正常的，不是管線斷了
（順帶一提，實測它偷懶不想的那幾次，答案也真的比較容易錯）。

## 圖片

```python
bot.ask("這是什麼顏色？", images=["red.png", "https://example.com/dog.jpg"])
```

本機檔案會轉成 base64 data URI，http(s) 網址直接原樣送。圖片是**按解析度計 token** 的，
一張大圖抵得上幾千字，送之前先降解析度通常是免費的優化。

## 後設：這一輪發生了什麼

```python
reply.finish_reason   # "stop" 正常講完 / "length" 話被切斷 / "tool_calls"
reply.usage           # {"prompt", "completion", "total", "cached", "reasoning"}
```

`"length"` **不是錯誤，是話講一半被 `max_tokens` 切掉了**，要不要接下去是你的決定
（沒有「續寫」這種原語，只能自己再問一輪）。

`usage["cached"]` 是命中前綴快取的部分。命中條件是**送出去的開頭一字不差**，所以
人格和工具定義要穩定待在最前面，會變的東西（時間、路徑）往後放 —— 在 `system`
裡塞當前時間等於把快取全砍了。這個欄位是你唯一能驗證有沒有命中的方法。

串流要拿到 `usage` 得送 `stream_options={"include_usage": True}`，`engine.py` 已經
自動開了。萬一哪個後端不吃這個參數，症狀會是串流整條爆掉，往這裡查。

## 不想留下記憶

`remember=False`，這次的一問一答都不寫進 `history`：

```python
reply = bot.ask("翻譯這句", remember=False)
```

要連 instance 都用完即丟就自己建一個，`LLM()` 很便宜（引擎也是，只是會多開一個
HTTP client）。
