# Controller：本地的方便封裝

`Handle` 是 agentloop 的原始直接把手：同一個 Python process 裡，可以看所有公開欄位、掛
callback、直接改資料，並呼叫 `pause()`／`resume()`／`end()`。

`Controller` 是第一個較舒服的本地用法，近似「C with class」：把一個 Round 的輸入、Handle、
runner 樣板收成一個 object。它不是安全邊界，也不取代或隱藏 Handle；其他人可直接用 Handle，
或建立完全不同的高階包裝。

```python
import agentloop

c = agentloop.Controller(bot, dispatch, "整理測試失敗")
c.start()                         # 一條本地背景 thread
c.wait()                          # waiting / paused / completed / error
with c.handle.edit():             # 需要時仍直接操作 Handle
    c.handle.prompt = "繼續修正"
c.resume()
result = c.join()
```

## 最小 API

- `Controller(bot, dispatch=None, prompt=None, handle=None, images=None)`：準備一個尚未開始的 Round。
  傳入既有 Handle 時，仍只可使用一次。
- `.handle`：完整、刻意公開的直接 Handle 逃生口。
- `.advance()`：只做一次 Step 或一整批 tools；返回時位於 `ready`、`waiting`、`paused` 或終止
  邊界，不會停在半個 operation。
- `.run()`：在目前 thread 一直跑到 Round 結束；語意等同 `agentloop.run()`。
- `.start()`／`.join()`／`.is_alive()`：包裝 `agentloop.threading` 的單一背景 thread，不是 pool。
- `.state`／`.now()`／`.pause()`／`.resume()`／`.end()`：常用狀態與控制的簡寫。
- `.wait(*states, timeout=None)`：委派給 Handle 的狀態等待，並保留 timeout 的 bool
  回傳值。沒有列 states 時，預設等到 `waiting`／`paused`／`completed`／`error`，
  避免快速結束的 Round 錯過單一 `waiting` 狀態。
- `.send(prompt, finish=False)`：只在 `waiting`／`paused` 邊界設定下一個 prompt 並恢復；返回
  Controller 自己，方便 REPL 使用。它不是任意時刻收件的 queue。

`ready` 已是安全邊界；此時 `pause()` 會立即成為 `paused`，`end()` 會立即完成，不必再補一次
`.advance()` 才提交控制要求。

同一個 Controller 只能選一種 runner 樣式：逐步 `.advance()`、前景 `.run()`，或背景
`.start()`。這避免一個 Handle 被兩個 runner 同時使用；並不限制你從 `.handle` 檢視、修改或
提出合作式 pause/end。

## 明確不負責

Controller 不做 database、持久化、Task queue、worker lease、revision、Candidate、scheduler，
也不提供 ACL、sandbox 或強制中止。這些是需要跨重啟、多 process、權限或副作用處理時才建立的
外圍 service／runtime 協定。`advance()` 可供那種未來 adapter 使用，但本身不是 durable command。
