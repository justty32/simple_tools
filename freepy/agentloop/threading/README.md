# agentloop.threading

`agentloop.threading` 是同步 agentloop 的內置背景執行工具。它只做一件事：
**建立一條 thread，在裡面呼叫原本的 `agentloop.run()`。**

核心的 Step、callback、tool、pause 與 waiting 語意完全不變。

## `dispatch` 是什麼

`dispatch` 是「模型使用的工具名稱 → 真正 Python 函式」的對照表：

```python
dispatch = {
    "read_file": read_file,
    "run_shell": run_shell,
}
```

模型要求呼叫 `read_file` 時，agentloop 會從 `dispatch["read_file"]` 找到要執行的函式。
它和 thread ownership 無關；背景工具只是把同一份 dispatch 傳給同步 `run()`。

## 最小用法

```python
from agentloop.threading import start

runner = start(bot, dispatch, "開始")
handle = runner.handle

# 主 thread 可以做別的事

result = runner.join()
assert result is handle
```

角色很單純：

```text
controller thread                 runner thread
-----------------                 -------------
runner = start(...)  -----------> agentloop.run(...)
handle.pause()                     正在跑 Step 或 tools
handle.wait_until_paused() <------ 到安全邊界後 paused
修改 handle
handle.resume()       -----------> 同一條 runner thread 繼續
runner.join()         <----------- Round 結束，返回 Handle
```

通常呼叫 `start()` 的 thread 是 controller；新建立的 thread 是唯一 runner。
callbacks、模型請求與整批工具都在 runner thread 同步執行，不會各自再開 thread。

## Handle 可以被多條 controller threads 持有

Handle 沒有 controller ownership；它是普通的 Python object，可以直接傳給新 thread：

```python
import threading

def observe(handle):
    handle.wait_for_state("paused", "completed", "error")
    print(handle.state)

threading.Thread(target=observe, args=(handle,)).start()
```

父 thread 和新 thread 拿到的是同一個 Handle，不是副本。也可以用 `queue.Queue` 把
Handle reference 傳給另一條 thread；agentloop 不需要另外提供 send/receive API。

需要區分兩件事：

- **controller sharing**：多條 threads 可以觀察、修改、pause 或 resume 同一個 Handle。
- **runner ownership**：同一個 Handle 只能被一條 runner thread 執行一次，不可交給
  第二個 `run()`／`start()`，完成後也不可重跑。

把 Handle 傳給另一個 controller 不算 ownership transfer；目前也沒有 controller
owner 可供轉移。若應用程式要求「只有一個 controller 可以寫」，應由應用程式自己用
queue、lock 或其他協議約束。

## 暫停、修改與繼續

```python
from agentloop.threading import start

runner = start(bot, dispatch, "先分析", handle=handle)
h = runner.handle

h.pause()                    # 只提出要求，立即返回
h.wait_until_paused()        # 等 runner 到達安全邊界

with h.edit():               # 跨 thread 的複合修改保持原子性
    h.prompt = "改成檢查另一個目標"
    h.tool_calls.clear()

h.resume()
result = runner.join()
```

`pause()` 不會切斷已開始的 operation：

- 模型正在產生 Step：提交 Step、跑完 `after_step`，再暫停於 tools 前。
- 工具批次正在執行：跑完整批、提交 results、跑完 `after_tools`，再暫停於下個 Step 前。

直接修改公開欄位仍然允許；如果需要一次一致地修改多個欄位或 nested list/dict，使用
`with handle.edit()`。不使用它時，競態風險由操作者承擔。

如果 controller 決定不再繼續，可以安全結束：

```python
h.end(reason="operator")       # 立即返回；不切斷已開始的 operation
result = runner.join()         # 等安全邊界完成後返回
```

若已在 `waiting`／`paused`，`end()` 會當場轉成 `completed` 並喚醒 parked
runner。若在 Step 中，它等 Step 和 `after_step` 完成後於 tools 前結束；若在
tool batch 中，則先完成整批與 `after_tools`。

### callback 裡建立 thread 的自鎖風險

callbacks 在 runner thread 且持有 Handle 的 `RLock` 時執行。callback 可以建立新
thread，但不可一邊持有這把 lock，一邊等待需要操作 Handle 的 child thread：

```python
def callback(handle):
    child = threading.Thread(target=lambda: handle.pause())
    child.start()
    child.join()  # 錯：child 等 Handle lock，callback 又在等 child
```

這會形成死鎖。可以讓 child 自行繼續，不在 callback 裡 `join()`；更簡單的做法通常是
讓 callback 直接修改 Handle 或回傳 `PAUSE`／`END`。

## waiting 會占住 runner thread

`Handle(auto_finish=False)` 在模型不再要求工具時進入 `waiting`，背景 thread 仍然活著：

```python
import agentloop
from agentloop.threading import start

h = agentloop.Handle(auto_finish=False)
runner = start(bot, dispatch, "開始", handle=h)

h.wait_for_state("waiting")
with h.edit():
    h.prompt = "再做一件事"
    h.auto_finish = True
h.resume()

result = runner.join()
```

這是 parked-runner 設計：resume 喚醒原本那條 thread，不會建立新 runner，也不需要把
Round 交接給第二次 `run()`。代價是每個 waiting／paused Round 都占用一條 thread。

## API

### `start(...)`

```python
start(
    bot,
    dispatch=None,
    prompt=None,
    handle=None,
    images=None,
    *,
    daemon=False,
    name=None,
)
```

建立並立即啟動一個 `BackgroundRun`。未傳 `handle` 時會建立新的 Handle。

`daemon=False` 是預設值，避免 Python 程序退出時無聲切斷尚未完成的 Round。若呼叫者
明確接受 daemon thread 可能來不及收尾，才設為 `True`。

### `BackgroundRun`

- `handle`：controller 與 runner 共用的公開 Handle。
- `thread`：底層 `threading.Thread`，供名稱、ident 等低階觀察使用。
- `is_alive()`：runner thread 是否仍活著。
- `join(timeout=None)`：等待 Round 結束，成功時返回同一個 Handle。
- `result`：runner 完成後的 Handle；完成前是 `None`。
- `error`：核心外層未處理的 runner 錯誤；正常時是 `None`。

若 `join(timeout)` 到期時 thread 還活著，會拋 `TimeoutError`；這只代表「這次不再等」，
**不會取消或終止 runner**。之後仍可再次呼叫 `join()`。

一般模型、工具或 callback 錯誤由 agentloop 核心寫入 `handle.state == "error"` 與
`handle.err`，所以 `join()` 正常返回該 Handle。Handle 重用等核心外層的程式契約錯誤
則由 `join()` 重拋。

若需要先建立、稍後才啟動，也可以直接使用：

```python
from agentloop.threading import BackgroundRun

runner = BackgroundRun(bot, dispatch, "開始")
# 先保存 runner 或完成其他設定
runner.start()
```

## Ownership 規則

- 一個 Handle 只能對應一個 Round、由一條 runner thread 擁有。
- 同一 Handle 不可同時交給兩個 `start()`／`run()`，完成後也不可重跑。
- 同一 bot 不應同時交給多個 runners，否則會競爭修改同一份 history。
- callbacks 在 runner thread 執行；callback 若做阻塞工作，也會阻塞整個 Round。
- 多個 controller threads 可以共用 Handle，但複合修改應自行用 `handle.edit()` 協調。

這裡的「thread」只指同一個 Python process 裡共享記憶體的 threads。`os.fork()` 後的
Handle 是另一份記憶體副本，狀態不會同步，而且 fork 時繼承的 lock 可能停在不安全狀態；
因此不支援把現有 Handle 當成跨 process 控制把手。跨 process 控制需要另外設計 IPC。

## 不負責的事情

這個子專案刻意不提供：

- thread pool 或同時執行大量 Rounds 的 scheduler；
- async API；
- 強制殺掉 model request、tool 或 Python thread；
- unsafe pause；
- tool batch 內的平行執行；
- waiting threads 的資源回收或持久化。

因此它適合少量 Round 的背景控制。若要同時管理大量長期 waiting agents，應由更上層的
runtime 決定 worker、queue、持久化與喚醒策略，而不是繼續擴大這個薄封裝。
