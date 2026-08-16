# 從單純的 loop 到現在

這份是給「記憶還停在單純 loop」的人看的。不講怎麼用（那在
[README](../README.md)），也不講怎麼加東西（那在
[ADDING-A-LOOP](ADDING-A-LOOP.md)）——**只講長出了什麼、為什麼**。

一次讀完大約十分鐘。

---

## 你記得的那個 loop 沒變

```cpp
for (;;) {
    auto call = queue.take();   // 取一個，空的就擋著等
    if (!call) break;           // 取到哨兵 = 收工
    handle(*call);              // 交給執行者
}
```

這**還是** `src/loop.cpp` 裡的全部。整個 `Loop` 類別 100 行，`Call` 這個
struct 的欄位也只多了一個 `env`。

長出來的東西沒有一個是改這裡，全部是**在旁邊接上去**的。所以如果你只想理解一件事：

> 核心是「一個 queue、一個 loop、一個 Executor 介面」。
> 其他每一樣都是插在這三個縫上的東西。

---

## 長出來的九件事，各自解決一個問題

按加入順序。**順序就是決策順序**——後面的常常是前面那個決定逼出來的。

### 1. `Executor` 介面 — 把 exec 關起來

**問題**：`exec` 是整套裡唯一有外部作用、唯一難測、唯一跟平台綁死的一段。

**做法**：`Loop` 不知道怎麼跑程式，它只認得 `Executor::run(Call) -> Outcome`。

**換到**：核心可以在**完全沒有子行程**的情況下測完。測試也照這條線分兩邊
（`test-core` 不開任何檔、不 fork；`test-exec` 才碰真的東西）。

### 2. `ExecExecutor` — 真的去跑

**問題**：怎麼跑一個程式，而且留下說得清楚的證據。

**做法**：`validate → unlink(exit) → 開三個檔 → source env → fork/exec → waitpid
→ 原子寫 exit`。

**踩到的**：
- ★ **`waitpid` 的原始 status 才分得出 `exit 137` 和 `SIGKILL`**。只看
  「128+signal」那個合成數字的話兩者一模一樣。
- ★ **exec 失敗要用 self-pipe 偵測**，否則「執行檔不存在」跟「程式自己回 127」
  分不開。
- ★ **fork 前要 `fflush(nullptr)`**，否則 parent 沒沖出去的 stdio buffer 會被
  子行程繼承，然後落進**證據檔**裡。這個只有 sanitizer 建置抓得到。

這個檔 686 行，是全專案最大的一個——**而且它應該是**。難的東西集中在一處，
比散在各處好。

### 3. `Router` — 依 `argv[0]` 分流

**問題**：`llm-ask` 那種東西有自己的節奏（等網路、限流）。跟一般 exec 擠同一條
loop 的話，一個慢的會把後面全部卡住。

**做法**：`argv[0]` 是**路由鍵**，決定進哪個 queue。整串精確比對，不猜 basename。

**連帶的改變**：★ `argv[0]` 因此**不必是路徑**——它可能是路徑、可能是 PATH 裡的
名字、也可能是純粹的邏輯名字（`llm-ask` 根本不會被 exec）。所以「`argv[0]` 該長
什麼樣」從共通驗證裡拿掉了，變成各個目的地自己的事。

### 4. `Pool` — 有上限的併發

**問題**：`Loop` 一次一個，對「跑外部程式」太保守；但也不能無上限。

**做法**：N 條 worker 共吃一個 queue。**上限就是 worker 數量**，用結構保證，
不是靠計數器檢查。

**能這樣做是因為**：`Queue::take()` 在收工後**重複呼叫都會回「收工了」**——
那個當初為了「重複 take 不要卡住」寫的行為，在這裡正好變成收工廣播。

### 5. `Channel` 介面 ＋ `PriorityQueue` — 排序是政策

**問題**：優先權要有，但它是**各條 loop 的政策**，不是呼叫的形狀。

**做法**：`Call` 裡**沒有** priority 欄位。`PriorityQueue` 收一個「怎麼算優先值」
的函式，各條 loop 自己給（llm-ask 就在那裡跑自己的 argparse 讀 `--priority`）。

為了讓各條 loop 能挑不同的 queue，`Router` 改吃 `Sink`、`Loop`／`Pool` 改吃
`Source`——那就是 `channel.hpp` 那 63 行的全部用途。

★ 同分**嚴格 FIFO**。沒有這個保證的話同優先權的順序不可重現，實務上就是有人餓死
而且重現不了。

### 6. 逾時與輸出上限 — 機制在核心，政策在 loop

**問題**：一個不結束的子行程會永遠佔住一條 worker。

**做法**：跟優先值同一招——`ExecExecutor` 收一個 policy 函式，`Call` 裡不加欄位。

**踩到的**：
- ★★ **砍的是行程群組，不是那一個 pid**。孫行程不會跟著死，它們被 init 收養後
  在背景一直跑。工作「被砍了」但機器上還有東西在動，最難查。
- 先 SIGTERM、等寬限、再 SIGKILL，讓子行程有機會自己收尾。
- `timed_out`／`output_capped` 是**獨立旗標**不是新 Status——否則被我們砍掉跟
  「外面有人砍它」都是 `Signalled{SIGKILL}`，分不開。

### 7. `ext/agent` — 第一個真擴充

**問題**：Agent 的 Round（一連串 Step 與 Tool 呼叫）怎麼跑。

**逼出來的設計**：直覺是 `while (!done) { dispatch(); wait(); }`——**不行**，
因為 `wait` 佔住 worker 會跟下游死鎖，而且一個跑三天的 Round 不可能靠一個 C++
堆疊活著。

所以 Round 的進度**存在磁碟上**，一次 tick 只做一小步：

```
tick = 讀上一個 child 的證據 → 問 controller → 派下一個 child（或收工）→ 立刻返回
                                                  │
child 做完 → 下游 observer 認出 user="agent:<dir>" → 推一個新 tick 回來
```

**沒有任何人在等任何人。**

★ 這個擴充**核心一行都沒改**，只用了三個公開的縫：`Executor`、`Sink`、
`Loop::Observer`。而且它不認識 LLM——prompt 與 tool 的知識全在 `Controller`
介面後面。

### 8. `WorkTracker` — 收工前先確認真的沒事了

**問題**（第 7 項逼出來的）：agent 派 child 給 exec，child 做完又推 tick 回 agent。
所以「兩個 queue 同時是空的」那一瞬間，可能只是工作正好在**兩條 loop 之間的半空中**。

**做法**：數的不是「排著幾個」，是「**進了系統還沒出去的有幾個**」。
`push` 成功 +1，executor 處理完 -1。

這個算法沒有窗口：處理途中生出來的新工作，是在**父工作 -1 之前**就 +1 的。

### 9. `ext/input` — 外部輸入

**問題**：在這之前，`Call` 只能由程式直接 `queue.push()`——它是函式庫，不是服務。

**做法**：一行一個 JSON 物件。stdin 或 Unix socket 兩條路，**共用同一個 dispatch**。

★ socket 是「**收下就回**」：`{"ok":true}` 只代表排進去了，client 自己去輪詢
`exit` 檔。這個選擇讓 loop 的形狀**完全不用動**——沒有任何從 `Outcome` 回到連線的
路徑。（「等到做完」那條路才會往回改 loop。）

★ 一行一個的價值不是好寫，是**一個壞掉的請求傷不到下一個**。

---

## 全景

```
  stdin (NDJSON)          Unix socket（AOS_LISTEN=1，收下就回）
        │                        │
        └────────┬───────────────┘
                 │  ext/input：一行 JSON -> Call ＋ 形狀驗證
                 ▼
              Router                    看 argv[0]，整串精確比對
                 │
        ┌────────┴─────────┐
        ▼                  ▼
  ┌───────────┐      ┌───────────┐
  │ exec queue│      │agent queue│      Channel：Queue 或 PriorityQueue
  └─────┬─────┘      └─────┬─────┘
        ▼                  ▼
    Pool(4)             Pool(1)         驅動：幾個同時跑
        │                  │
        ▼                  ▼
  DropArgv0Executor   AgentExecutor     Executor：你的邏輯
        │                  │
        ▼                  │ 派 child（新的 Call，自己的證據路徑）
  ExecExecutor  ◀──────────┘
        │
        └── child 做完 ──▶ continuation_observer ──▶ 回 agent queue

  WorkTracker 橫跨全部：push +1、處理完 -1，歸零才收工
```

---

## 檔案地圖

**核心 `src/`（1942 行）** — 沒有一個檔認識 exec 以外的世界：

| 檔 | 行 | 一句話 |
|---|---|---|
| `call.hpp/.cpp` | 133 | 固定的 struct ＋ 純函式驗證 |
| `channel.hpp` | 63 | `Sink`／`Source`／`Channel` 三個介面 |
| `queue.hpp/.cpp` | 115 | FIFO，哨兵收工 |
| `priority_queue.hpp/.cpp` | 146 | 會排序的 Channel，優先值由你給 |
| `loop.hpp/.cpp` | 219 | **核心 loop** ＋ `Executor`／`Outcome` |
| `router.hpp/.cpp` | 156 | 依 `argv[0]` 分流 |
| `pool.hpp/.cpp` | 170 | N 條 worker 共吃一個 queue |
| `tracker.hpp/.cpp` | 128 | 收工前確認系統靜下來 |
| `exec.hpp/.cpp` | 812 | **唯一碰外面的**：fork／exec／waitpid／證據檔 |

**擴充 `ext/`（1259 行）** — 只用核心的公開縫，核心不認識它們：

| | 行 | |
|---|---|---|
| `agent/` | 373 | AOS 的 Round loop |
| `input/json` | 264 | 只認字串與字串陣列的嚴格 parser |
| `input/ndjson` | 216 | 一行 JSON -> `Call` |
| `input/socket` | 346 | Unix socket，收下就回 |

**`bin/main.cpp`（211 行）** — 只有接線，沒有邏輯。

**測試 2259 行**，390 項檢查，分五支：

| | 碰外面？ |
|---|---|
| `test-core` (129) | 否。一個子行程都不起、一個檔案都不開 |
| `test-exec` (127) | 真的 fork、真的寫檔 |
| `test-agent` (47) | 真的 Round 驅動真的子行程 |
| `test-input` (65) | 純解析 |
| `test-socket` (22) | 真的 socket |

---

## 五條貫穿全部的規矩

如果只記五件事，記這五件。它們每一條都在多個地方被守著。

### 一、`Rejected` 保證什麼都沒發生

`Rejected`（形狀不對）和 `LaunchError`（試過了但沒起來）的差別是**有沒有發生過
事情**。這個區分決定了呼叫端能不能安全重跑。

### 二、`Unknown` 不是失敗

它是「我沒能問出結果」。不可以當成失敗，更不可以據此自動重跑——可能已經做過了。
所以它**不寫 exit 檔**：檔案不在才代表還不知道。

### 三、一個 `Call` 的 exit 檔只能有一個人寫

`exit` 檔的全部意義是「它在 = **這一次**的結論已知」。兩個人搶著寫，那句話立刻
不成立。所以 fan-out 生出來的每個新 `Call`，三個證據路徑**都要換成自己的**。

### 四、機制在核心，政策在各條 loop

優先權、逾時、輸出上限——`Call` 裡**都沒有**這些欄位，也不會有。核心提供機制
（怎麼排序、怎麼砍），各條 loop 提供函式決定政策。

這是為什麼 `Call` 到現在還是那麼小。

### 五、不要在 Executor 裡同步等另一條 loop

會死鎖，而且是那種要湊巧才發生、幾乎重現不了的。正確的形狀是**把後續工作也
變成一個 Call**：A 做完推 B，B 做完推 C。誰都不等誰。

`ext/agent` 整個設計就是這條規矩的產物。

---

## 我想做 X，該看哪

| | |
|---|---|
| 跑起來看看 | [README](../README.md) 開頭 |
| 加一條新 loop | [ADDING-A-LOOP.md](ADDING-A-LOOP.md) |
| 看一個真的擴充長什麼樣 | [ext/agent/README.md](../ext/agent/README.md) |
| 改 exec 的行為 | `src/exec.cpp` 的檔頭註解，**改完跑 `make sanitize`** |
| 知道還缺什麼 | [TODO.md](TODO.md) |
| 理解某個奇怪的決定 | 該檔的檔頭註解。★ 標記的都是踩過才知道的 |

---

## 還沒有的

四項，都在 [TODO.md](TODO.md) 裡寫了為什麼卡著：

1. **`Unknown` 之後怎麼辦** — 不能自動重跑，但也不能放著。誰來看、怎麼介入。
2. **搶佔** — 優先權只影響還在排隊的；已經在跑的不讓位。
3. **Task tree** — agent 只有一層 Round → child，還沒有多層。
4. **監看目錄** — socket 做完了，目錄那條還沒。
