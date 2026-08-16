# agent —— AOS 的 Round loop（擴充）

★★ **核心（`src/`）一行都不用改，也一行都不認識 agent。**
這裡只用三個公開的縫：`Executor`（做事）、`Sink`（把工作交出去）、
`Loop::Observer`（被通知做完了）。

名詞照 [`agent-machine/full/10-STEP-TOOL-ROUND.md`](../../../agent-machine/full/10-STEP-TOOL-ROUND.md)：

| | |
|---|---|
| **Step** | 有原子操作角色的 Function |
| **Tool** | Agent 可用的 Function，最底層就是一個 Linux process |
| **Round** | 由一串 Step 與 Tool 呼叫組成的 Function |
| **Task** | 正在執行中的 Function 實例（**不是 PID**）|

```
                  ┌──────────────────────────────┐
                  │  agent loop（一次一個就夠）   │
   tick ─────────▶│  讀證據 → 問 controller       │
                  │        → 派下一個 child       │
                  └──────────┬───────────────────┘
                             │ push（不等）
                             ▼
                  ┌──────────────────────────────┐
                  │  exec loop（四併發）          │
                  └──────────┬───────────────────┘
                             │ child 做完
                             │ observer 認出 user="agent:<dir>"
                             └──────▶ 推一個新 tick 回 agent queue
```

**沒有任何人在等任何人。**

## ★★ 為什麼 Round 是狀態機，不是 while 迴圈

直覺會這樣寫：

```cpp
while (!done) { auto call = decide(); dispatch(call); wait(call); }   // ✗
```

不行，兩個理由：

1. **死鎖。** `wait` 把這條 worker 佔住，而下游滿載時它的工作可能間接要等
   agent loop 有空。這正是 [`docs/ADDING-A-LOOP.md`](../../docs/ADDING-A-LOOP.md)
   6-4 那一條。
2. **Round 會跨行程重開。** 一個跑三天的 Round 不可能靠一個 C++ 堆疊活著。

所以進度存在磁碟上，一次 tick 只做一小步：

```
一次 tick = 讀上一個 child 的證據 → 問 controller → 派下一個 child（或收工）
```

派完就返回，worker 立刻放掉。這就是文件那句「Round controller 讀取已完成 child
的可信 Return，**保存自己的進度**，再決定下一個 Function Call」。

## ★★ 這一層不認識 LLM

> 「AOS Machine 不解析模型回覆；Agent controller 才懂 prompt、messages、
> tool requests 與產品失敗。」

prompt、tool schema、模型回覆怎麼拆成 tool call —— 全部在 `Controller`
這個介面**後面**，連 agent loop 自己都不碰。

模型呼叫本身照文件建議「包成 curl-like executable adapter」：它就是一個普通的
`exec` Call，跟別的 Tool 沒有兩樣。所以這個擴充**零網路、零 JSON、零第三方相依**。

## 你要寫的只有一個函式

```cpp
class MyController : public aosagent::Controller {
  public:
    Decision decide(const Round &round, const ChildResult &last) override {
        // last.exists  有沒有上一個 child
        // last.done    ★ exit 檔在不在。不在 = 還不知道，不是失敗
        // last.code    done 時才有意義
        // last.stdout_path / stderr_path  自己去讀

        if (last.exists && !last.done) return Decision::wait();   // 還沒好

        if (夠了) return Decision::finish(0);

        Call next;
        next.argv = {"exec", "/usr/local/bin/llm-adapter", "--prompt-file", …};
        return Decision::dispatch(next);
        // ★ 只要填 argv（和可選的 env／stdin）。stdout／stderr／exit／cwd
        //   由 agent loop 填成 steps/<n>/ 底下的路徑——你不要自己填。
    }
};
```

> ⚠ `decide()` **不可以阻塞**、不可以等 child、不可以打網路。
> 它只做一件事：看著證據，決定下一步。要打網路就派一個 child 去打（那才是 Tool）。

## 接線

```cpp
Queue agent_queue, exec_queue;
Router router;
router.route("agent", agent_queue);
router.route("exec",  exec_queue);

MyController controller;
AgentExecutor agent_executor{controller, exec_queue};
Pool agent_pool{agent_queue, agent_executor, 1};    // 只做決定，一個就夠

ExecExecutor raw;
DropArgv0Executor gated{raw};
Pool exec_pool{exec_queue, gated, 4,
               aosagent::continuation_observer(agent_queue)};   // ★ 續跑接這裡

router.dispatch(start_round("/var/aos/rounds/r-42"));   // 開跑
```

收工照資料流的順序：`agent_queue.close(); agent_pool.join();`
再 `exec_queue.close(); exec_pool.join();`

## 磁碟上長這樣

```
<round>/
  state          step=<n> / finished=<0|1> / code=<n>
  exit           Round 自己的結論（一個數字，跟 Call 的 exit 同慣例）
  tick.out/err/exit
  steps/1/       第 1 個 child 的證據
    stdin stdout stderr exit
  steps/2/
  …
```

`state` 走 `write_durable()`（`.tmp` → fsync → rename → fsync 目錄），
理由跟 exit 檔一樣：進度說謊比沒有進度更糟。

## ★★★ 順序：先存進度，再派工

```cpp
round.step = next;
save_round(round);        // 1. 先存
downstream_.push(child);  // 2. 再派
```

**反過來會壞。** push 成功但存檔前崩潰，重開後 `round.step` 還是舊的，
於是我們會再派一次 —— 而那個 child 可能已經跑過了，外部作用發生兩次。
這正是 AOS 那條「先提交 dispatch intent，再真正啟動」。

反方向的失敗（存了但沒派出去）是**安全**的：Round 停在那一步、child 的 exit 檔
永遠不出現，看起來就是「還不知道」——而那本來就不該自動重做。**停住比重複好。**

## 幾個刻意的選擇

- **child 的證據路徑由 agent loop 填，不由 controller 填。**
  那條「一個 `Call` 的 exit 檔只能有一個人寫」的鐵則，這樣才不可能被寫錯。
- **`child.user = "agent:<round 目錄>"`。** 不是硬掰用途——`user` 的定義就是
  「誰發起的」，而發起這個 child 的正是那個 Round。續跑靠它認人，
  而下游完全不認識 agent。
- **失敗與 `Unknown` 的 child 也會叫醒 Round。** 「失敗了怎麼辦」是 controller
  的政策，不是續跑機制的。`Unknown` 的時候 controller 拿到 `done=false`，
  由它自己決定要等、要放棄、還是要人來看。
- **已收工的 Round 再被 tick 是安全的**（重複的續跑觸發）。不報錯、不重做、
  直接回原本的結論。

## 這個擴充還沒有的

- **沒有 Task tree。** 只有一層 Round → child。child 自己再開 children
  （文件講的 Task tree）還沒做。
- **沒有 pause／恢復。** 狀態是續得下去的，但沒有「停在這裡等人」的指令。
- **沒有重試。** `Unknown` 之後怎麼辦完全交給 controller，核心沒有政策。
- **沒有 Tool schema、沒有 Tool Result 配對。** 那是真的 controller 要做的。
- **`state` 沒有 Task ID。** 文件說 ID 在 durable accept 時配置，這裡還沒有
  admission 那一層。
