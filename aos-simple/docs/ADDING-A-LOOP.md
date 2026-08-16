# 怎麼加一條新 loop

這份是給要在 aos-simple 上長出新 loop 的人（或 AI）看的。
讀完應該能自己加一條、並且知道 loop 之間怎麼把工作交來交去。

先讀 [README](../README.md) 的〈一次呼叫〉那節，知道 `Call` 長什麼樣再回來。

---

## 1. 一條 loop 是什麼

三個零件拼起來，沒有第四個：

```
        Router
          │ push
          ▼
    ┌───────────┐
    │  Channel  │   排隊的地方。Queue（FIFO）或 PriorityQueue（自己排）
    └───────────┘
          │ take
          ▼
    ┌───────────┐
    │   驅動    │   Loop（一次一個）或 Pool（N 個併發）
    └───────────┘
          │ run(call)
          ▼
    ┌───────────┐
    │ Executor  │   ★ 你的邏輯全部在這裡
    └───────────┘
```

- **Channel** 是資料結構，決定「誰先被拿到」。
- **驅動**是執行緒模型，決定「同時幾個在跑」。
- **Executor** 是你的東西，決定「拿到之後做什麼」。

加一條新 loop = 挑一個 Channel、挑一個驅動、寫一個 Executor、去 Router 註冊一個名字。
**核心一行都不用改。**

---

## 2. 最小的一條

假設要加一條 `llm-ask`。

```cpp
#include "src/loop.hpp"
#include "src/priority_queue.hpp"
#include "src/pool.hpp"
#include "src/router.hpp"

using namespace aossimple;

// ── 你的邏輯 ──────────────────────────────────────────────────────
class LlmAsk : public Executor {
  public:
    Outcome run(const Call &call) override {
        // call.argv[0] 是 "llm-ask"（路由鍵，見第 5 節）
        // call.argv[1..] 是你自己的參數，怎麼解析是你的事

        // …做事…

        return Outcome::exited(0);
    }
};

// ── 接線 ──────────────────────────────────────────────────────────
PriorityQueue llm_queue{[](const Call &c) { return parse_priority(c.argv); }};
LlmAsk llm_executor;
Pool llm_pool{llm_queue, llm_executor, 2};   // 同時最多兩個

router.route("llm-ask", llm_queue);
```

就這樣。收工的時候 `llm_queue.close(); llm_pool.join();`。

---

## 3. 挑 Channel

| | 什麼時候用 |
|---|---|
| `Queue` | 純 FIFO。先來先做。**預設就選這個** |
| `PriorityQueue` | 要排序。優先值由**你給的函式**算 |

`Call` 裡**沒有** priority 欄位，也不會有——那是你這條 loop 的政策，不是呼叫的形狀。
所以 `PriorityQueue` 收一個函式：

```cpp
PriorityQueue q{[](const Call &c) -> long {
    // 你自己的 argparse。--priority 5 之類。
    for (std::size_t i = 0; i + 1 < c.argv.size(); ++i) {
        if (c.argv[i] == "--priority") return std::stol(c.argv[i + 1]);
    }
    return 0;   // 沒給就是 0
}};
```

數字**大的先做**。同分嚴格 FIFO（有測試守著）——沒有這個保證的話，
一堆同優先權的工作順序會不可重現，實務上就是有人餓死而且重現不了。

> ⚠ 排序只發生在**還排在 queue 裡**的東西之間。已經被拿走的不會被搶佔，
> 所以一個低優先權的長工作還是會佔住一條 worker 直到它結束。要搶佔就得
> 能砍掉跑到一半的工作，那是另一件事。

---

## 4. 挑驅動

| | 同時幾個 | 什麼時候用 |
|---|---|---|
| `Loop::run()` | 1 | 有共用狀態、或本來就該序列化的事 |
| `Pool` | N | 大部分時間在等（跑外部程式、打網路） |
| `Loop::drain()` | 1，不擋 | **測試**用。做完手上的就回來 |

```cpp
Loop loop{queue, executor};
std::thread t{[&] { loop.run(); }};      // 一次一個

Pool pool{queue, executor, 4};           // 同時最多四個，執行緒它自己開
```

`Pool` 的上限就是 worker 數量，用結構保證，不是靠計數器檢查。
它可以直接共吃一個 Channel，是因為 `take()` 在收工後**重複呼叫都會回
「收工了」**——N 條 worker 都收得到那個廣播。

> ⚠ `Pool` 裡你的 `Executor` 會被多條執行緒同時呼叫，共用狀態要自己鎖。
> observer 已經被 `Pool` 上鎖串起來了，寫 observer 不必想併發。

---

## 5. 去 Router 註冊

```cpp
router.route("llm-ask", llm_queue);
```

比對的是 `call.argv[0]`，**整串精確比對**。不取 basename、不做前綴、不做萬用字元。
`/opt/somewhere/llm-ask` **不會**自動命中——自動猜 basename 的話
`/opt/evil/llm-ask` 也會被路由走。要它命中就把那個字串也註冊上去。

### 路由鍵要不要吃掉？

看你的 Executor 期待 `argv[0]` 是什麼。

- **要跑外部程式**（像 `exec`）：要吃掉，不然它會去找一支叫 `exec` 的執行檔。
  包一層 `DropArgv0Executor`：

  ```cpp
  ExecExecutor raw;
  DropArgv0Executor gated{raw};      // exec /bin/ls -la  ->  跑 /bin/ls -la
  Pool pool{queue, gated, 4};
  ```

- **自己處理**（像 `llm-ask`）：不用吃，留著當自我識別也行。

### `dispatch()` 的四種回覆一定要處理

```cpp
switch (router.dispatch(call).result) {
    case Router::Result::Routed:   break;  // 進了註冊的 queue
    case Router::Result::FellBack: break;  // 進了 fallback
    case Router::Result::NoRoute:  /* 沒命中又沒 fallback */ break;
    case Router::Result::Closed:   /* 目的地收工了 */ break;
}
```

★ 後兩種**不可以安靜忽略**。那代表這通呼叫沒有人會處理，等它的人會永遠等。

---

## 6. ★★ loop 之間怎麼傳 Call

這一節是重點。有兩種形狀，不要搞混。

### 6-1 路由：執行**之前**決定去哪

Router 看 `argv[0]` 就把它送走，沒有任何 loop 碰過它。這是第 5 節在講的。

### 6-2 Fan-out：執行**之中**生出新工作

你的 Executor 手上抓一個 `Sink&`，做事的同時往別條 loop 推**新的** `Call`：

```cpp
class Planner : public Executor {
  public:
    explicit Planner(Sink &downstream) : down_(downstream) {}

    Outcome run(const Call &call) override {
        for (int i = 0; i < 3; ++i) {
            Call child = call;                       // 從自己複製，路徑等一下換掉
            child.argv = {"exec", "/bin/do-part", std::to_string(i)};

            // ★★ 每個新 Call 一定要有自己的證據路徑
            const std::string tag = ".part" + std::to_string(i);
            child.stdout_path = call.stdout_path + tag;
            child.stderr_path = call.stderr_path + tag;
            child.exit_path   = call.exit_path   + tag;

            if (!down_.push(child)) {
                return Outcome::launch_error(0, "下游 loop 已收工，交不出去");
            }
        }
        return Outcome::exited(0);   // 自己這通照常給結論
    }

  private:
    Sink &down_;
};
```

接線的時候把下游的 queue 交給它：

```cpp
Planner planner{exec_queue};
Pool plan_pool{plan_queue, planner, 2};
```

要能經過 Router 分流（而不是寫死送去某個 queue）的話，抓 `Router&` 也可以——
`Router::dispatch()` 就是一個帶決策的 `push`。

### 6-3 鐵則

> **★★ 一個 `Call` 的 `exit_path` 只能有一個人寫。**

`exit` 檔的全部意義是「它在 = **這一次**的結論已知」。兩個人搶著寫同一個路徑，
那句話立刻不成立——你分不出讀到的是誰的結論。所以：

- **不要**把同一個 `Call` 推去兩個地方。
- fan-out 生出來的每個新 `Call`，`stdout`／`stderr`／`exit` **都要換成自己的**。
  只換 `exit` 不換 `stdout` 一樣會互相蓋掉。
- 父工作要不要等子工作，是你的事；但父工作的 `exit` 檔只講**父工作**的結論。

`test_fan_out`（`test/test_core.cpp`）就是這個模式的可執行版本，
最後那兩項斷言守的就是「三個 exit 路徑互不相同」。

### 6-4 兩個會咬人的地方

**① 不要在 Executor 裡同步等另一條 loop 的結果。**

```cpp
// ✗ 危險
Outcome run(const Call &call) override {
    down_.push(child);
    wait_for(child.exit_path);   // ← 這裡擋住了
    ...
}
```

你這條 worker 被佔住了。如果下游滿載、而下游的工作又（直接或間接）要等你這條
loop 有空——就是死鎖，而且是那種要湊巧才會發生、很難重現的。

要做「等結果」的話，正確的形狀是**把後續工作也變成一個 Call**：A 做完推 B，
B 做完推 C。誰都不等誰。

**② fan-out 要有終點。**

A 推給 B、B 又推回 A，那就是一個無窮迴圈，而且因為 Channel 是無上限的，
它會一路吃記憶體到爆。畫一下你的 loop 之間的圖，確認它是**有向無環**的；
真的需要環（重試之類），就在 `Call` 裡帶一個計數（例如 argv 加一個 `--attempt 2`）
並自己設上限。

---

## 7. 結論怎麼給

`Executor::run` 回一個 `Outcome`。五個 Status 各自只代表一件事：

| | 意思 | exit 檔 |
|---|---|---|
| `Rejected` | 形狀不對，**根本沒開始做** | `125` |
| `Exited` | 做完了，`code` 有意義 | `code` |
| `Signalled` | 被信號殺死 | `128+signal` |
| `LaunchError` | 起不來 | `127` 找不到／`126` 其他 |
| `Unknown` | ★ **不知道** | **不寫** |

三條規矩：

1. **`Rejected` 保證什麼都沒發生。** 開過檔、跑過一半就不能用它——那是
   `LaunchError` 或 `Unknown`。這個區分決定了呼叫端能不能安全重跑。
2. **`Unknown` 不是失敗。** 它是「我沒能問出結果」。呼叫端不可以把它當失敗，
   更不可以據此自動重跑（可能已經做過了）。所以它**不寫 exit 檔**——
   檔案不在才代表還不知道。
3. **不要丟例外。** `Loop` 會接住並轉成 `Unknown`，但那會讓一次真的執行看起來像
   「不知道」，而這兩件事的後果完全不同。有結論就回結論，包含失敗的結論。

### 你的 Executor 要不要自己寫 exit 檔？

要——**如果你的 loop 是這個 `Call` 的終點**。`ExecExecutor` 就是自己寫的。
寫的時候**直接用 `aossimple::write_durable()`**（`src/exec.hpp`），不要自己寫一遍：
它是 `.tmp` → `fsync` → `rename` → `fsync` 目錄，三段各自擋不同的災難、順序也
有意義，理由在 `src/exec.cpp` 的實作註解裡。

如果你的 loop 只是把工作**轉交**給別人（自己不下結論），那就不要寫，
讓真正執行的那一層去寫。

---

## 8. 收工的順序

```cpp
queue.close();   // 1. 先關 queue：排在前面的照做完，之後 push 回 false
pool.join();     // 2. 再等 worker 結束
```

反過來會永遠等。`Pool` 的解構子會自己 `join()`，但**不會**幫你 `close()`——
它不知道還有沒有別人要往那個 queue 放東西。

多條 loop 串起來的時候，**照資料流的順序關**：先關最上游，等它空了再關下游。
先關下游的話，上游還在跑的工作會推不出去。

---

## 9. 檢查清單

加完一條新 loop，對一遍：

- [ ] Executor 不丟例外
- [ ] `Rejected` 只用在「什麼都還沒做」的情況
- [ ] 不確定的時候回 `Unknown`，而且**不寫** exit 檔
- [ ] 如果是終點，exit 檔走原子寫入（`.tmp` → fsync → rename → fsync 目錄）
- [ ] fan-out 出去的每個新 `Call` 都有自己的 `stdout`／`stderr`／`exit`
- [ ] 沒有在 Executor 裡同步等另一條 loop
- [ ] loop 之間的圖是有向無環的
- [ ] 用 `Pool` 的話，Executor 的共用狀態有鎖
- [ ] `dispatch()` 的 `NoRoute`／`Closed` 有處理，不是安靜忽略
- [ ] 收工是先 `close()` 再 `join()`
- [ ] 測試放對邊：不碰檔案系統的進 `test_core`，會 fork／寫檔的進 `test_exec`
- [ ] 動過會 fork 的程式碼就跑一次 `make sanitize`

---

## 10. 完整範例

`test/test_core.cpp` 裡有兩個可以直接抄的：

- `test_priority_queue()` — `PriorityQueue` ＋ 從 argv 解析 `--priority`
- `test_fan_out()` — `FanOut` executor，一個進來三個交下去，每個有自己的證據路徑

`test/test_exec.cpp` 的 `test_exec_route()` 是完整的一條：
`Router` → `PriorityQueue`／`Queue` → `Pool(4)` → `DropArgv0Executor` → `ExecExecutor`。

### 最完整的範例：agent 擴充

[`ext/agent/`](../ext/agent/README.md) 是照這份文檔長出來的第一個真擴充，
**核心一行都沒改**。它示範了這份文檔講的每一件事：

- 用 `Executor` 當縫，把政策（Agent 的智慧）關在 `Controller` 介面後面
- 用 `Sink` 把工作交給另一條 loop（第 6-2 節的 fan-out）
- 用 `Loop::Observer` 做**續跑**——這是第 6-4 節「不要同步等別人」的正解：
  child 做完之後由下游的 observer 把上游叫醒，誰都不等誰
- 每個 child 的證據路徑由派工的那一層統一填，讓「一個 exit 檔只有一個人寫」
  這條鐵則不可能被寫錯
- 「先存進度、再派工」——順序反了會讓外部作用發生兩次

要加第二個擴充的話，先讀它。
