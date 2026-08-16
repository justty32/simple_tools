# aos-lisp

AOS 核心 loop 的 Lisp 版。Janet（1.41.2），**零第三方相依**。

## 兩層，各自跑得起來

| | 是什麼 | 進入點 |
|---|---|---|
| [`aos/`](aos/) ＋ [`bin/`](bin/) | **純 Janet 參考實作**。一個 loop、一張 env、一個 queue。可以單獨跑、單獨測 | `janet bin/aos-lisp.janet` |
| [`plugin/`](plugin/README.md) | **C++ 入口版**。掛進 aos-core 當 `.so` 外掛，C++ 管分派與 process，Janet 當嵌入式腳本 | `aos lisp eval <loop> <form>` |

純 Janet 那層是**可執行的參考模型**：它把求值語意用最少的東西講清楚，並且是
C++ 版的行為基準。C++ 版沿用它的求值語意（同一組 sigmask、同一套錯誤變資料的規矩），
但接管了三件 Janet 做不到或做不好的事——多 loop、真正的 `waitpid`、aos-core 的傳輸層。
兩層各自有測試，都要過。

**先讀哪個**：想懂這套怎麼運作，讀這一頁；想接來用、或想知道為什麼要有 C++，
讀 [`plugin/README.md`](plugin/README.md)。

---

## 純 Janet 層

整台機器就是這五行：

```janet
(forever
  (def t (queue/take (rt :queue)))   # 取一個，空的就擋著等
  (if (nil? t) (break))              # 取到哨兵 = 收工
  (execute rt t))                    # 在那張 env 裡把它跑掉
```

其他所有東西都是為了讓這五行成立。

## 馬上試

```sh
janet bin/aos-lisp.janet                                  # 互動
echo '(+ 1 2) (def x 7) (* x 6)' | janet bin/aos-lisp.janet
jpm test                                                  # 跑測試
jpm build && echo '(aos/help)' | ./build/aos-lisp         # 編成單一執行檔
```

```
$ printf '(def x 7)\n(* x 6)\n(error :boom)\n(aos/submit (quote (def y 1)))\n' | janet bin/aos-lisp.janet
#1 :done      (def x 7) => 7
#2 :done      (* x 6) => 42
#3 :failed    (error :boom) !! :boom
error: boom
  in thunk [eval] (tail call) on line 3, column 1
#4 :done      (aos/submit (quote (def y 1))) => @{:form (def y 1) :id 5 :state :queued}
#5 :done      (def y 1) => 1
```

第 2 行看得到第 1 行定義的 `x`；第 3 行炸了但第 4 行照跑；第 4 行排出來的
任務變成第 5 行，同一輪就跑掉了。

## 跟 aos-core 的對照

同一個骨架，換掉「一次呼叫」是什麼。

| | `aos-core`（C99） | `aos-lisp`（Janet） |
|---|---|---|
| 一次呼叫是 | argv ＋ stdin/stdout/stderr ＋ exit status | 一個 **form**（list）＋ 一個**值** |
| 派發 | 命令樹，名字對到 C 函式 | 不派發，直接 `eval` |
| 跨呼叫狀態 | `runtime.c` 裡的 struct，要鎖 | **一張長存的 env**，不用鎖 |
| 擴充 | 重編模組或 `dlopen` 一個 `.so` | `(def …)` 一個新名字進 env，**執行期就生效** |
| 併發 | 一條連線一條 thread，全部阻塞 | 一次跑一個，**完全不併發** |
| 隔離邊界 | 行程（外掛在同一個行程裡，所以怕它 crash） | env（任務炸掉只是一筆 `:failed`） |

最大的差別是**擴充**那一列。aos-core 要加一個命令得改 `registry.c` 再重編，或
編一個 `.so` 出來 `dlopen`；這一版就是往 env 裡 `def` 一個名字，下一個任務立刻看得到。
代價是它沒有 ABI 這回事——沒有版本檢查、沒有型別、撞名就是後來的贏。

## 五個檔

| 檔 | 負責 |
|---|---|
| [`aos/runtime.janet`](aos/runtime.janet) | **核心 loop**、submit、任務表、`aos/*` 的 host |
| [`aos/queue.janet`](aos/queue.janet) | FIFO。★ 收工用哨兵，不用 `ev/chan-close` |
| [`aos/task.janet`](aos/task.janet) | 一個任務的狀態機。純資料，不碰 fiber 也不碰 queue |
| [`aos/evaluate.janet`](aos/evaluate.janet) | **唯一碰 fiber 的地方**：在指定 env 求值，出錯變成資料 |
| [`aos/env.janet`](aos/env.janet) | 造那張獨立 env，注入 `aos/*` |
| [`aos/unix.janet`](aos/unix.janet) | **擴充**：把一次 Unix 呼叫變成一個 list（見下） |

[`bin/aos-lisp.janet`](bin/aos-lisp.janet) 是驅動程式，**不是核心的一部分**：
核心只認得已經 parse 好的 form，「字串從哪來」是驅動的事。之後要接 Unix socket、
接檔案、接別的行程，換掉的都只有那個檔。

## 那張 env

```janet
(def e (make-env))    # 以 root-env 為 prototype
```

所以任務看得到 Janet 標準函式庫的全部，任務的 `(def …)` 落在這張表、不會污染 host，
host 自己的 env 任務也看不到。

**這是命名空間隔離，不是安全沙箱。** 任務能 `(os/execute …)`、能讀檔、能開 socket。
這跟 aos-core 的立場一致：那條 socket 權限 `0600`，能送東西進來的人本來就等於有你的
shell，再在語言層擋一層是自欺欺人。真要沙箱就換掉 `env/make` 的 `parent`，自己餵一張
白名單表——那個參數留著就是為了那天。

### env 不是單例

現在一個 runtime 配一張 env，**只是因為現在這樣最簡單**。所以它是 `rt` 上的一個欄位、
是 `evaluate/in-env` 的一個參數，程式裡沒有任何地方假設它只有一張：

```janet
(def a (runtime/new))
(def b (runtime/new))    # 兩張 env，互相看不到對方定義的東西
```

要變成一個任務一張、或一個 session 一張，動的是 `execute` 那一行怎麼挑 env。

## `aos/*` 內建

不是 import 進去的，是 runtime 造一張 host 函式表**注入**的——所以 env 那一層完全
不認識 runtime。這是 aos-core 給外掛 `const aos_host *` 的同一招。

| | |
|---|---|
| `(aos/submit form)` | 排一個新任務，回傳 task。排在隊尾，同一輪就會跑到 |
| `(aos/self)` | 正在跑的這個 task |
| `(aos/task id)` / `(aos/tasks)` | 查一個 / 全部 |
| `(aos/resume id &opt v)` | 把 `:paused` 的任務接回去 |
| `(aos/status)` | uptime、做過幾個、queue 還剩幾個 |
| `(aos/env)` | 這張 env 自己，拿去 `all-bindings` 自省 |
| `(aos/help)` | 列出上面這些 ＋ 這張 env 自己有的名字 |
| `(aos/stop)` | 做完手上排著的就收工 |

## 一次只跑一個

`execute` 回來之前不會去取下一個。所以任務之間沒有交錯，那張共用的 env 不需要鎖，
也不可能有 race。

代價是**一個慢任務會擋住全部**——IO 也一樣，任務裡 `(ev/sleep 10)` 就是整台停 10 秒。
這是刻意的：aos-core 選的是「一條連線一條 thread、全部阻塞」，把併發推到連線層；
這一版把併發推到 0，換那張長存 env 連鎖都不用。想併發的話改的是 `execute`
（換成 `ev/go`），loop 本身不用動——但那時候「一張共用 env」就會需要一個說法了。

## 幾個踩過的坑

### ★★ sigmask 不可以是 `:a`

直覺會想「把全部信號都擋下來最安全」。這會**安靜地弄壞所有 IO**：

```
mask :a           (ev/sleep 0.05)  =>  (:suspended nil)   ← 永遠不會完成，不報錯
mask :ey01234567  (ev/sleep 0.05)  =>  (:dead :slept)     ← 0.050 秒
```

因為 ev 模組**就是用信號實作阻塞 IO 的**：user9 是 await、user8 是 interrupt。
`:a` 把這兩個一起攔下來，於是「等 IO」被我們攔截後沒人接手，任務停在 `:suspended`
而不是 `:dead`，狀態表上看起來還像它在跑。所以要一個一個列，
故意留下 `:8`、`:9` 給 ev。`test/evaluate.janet` 有一項專門守著這件事。

### ★ `ev/chan-close` 會丟掉還沒取走的東西

```janet
(ev/give ch :a) (ev/give ch :b)
(ev/count ch)      # => 2   東西還在
(ev/chan-close ch)
(ev/take ch)       # => nil ★ 但一個都讀不到了
```

對這裡來說那等於「叫它收工，結果排在前面的任務被安靜丟掉」。aos-core 的
`aos daemon stop` 是**做完手上的事之後**才收工，這一版要一樣，所以 `queue/close`
是往隊尾放一個私有哨兵：哨兵前面的照做，取到哨兵才回 `nil`。

### `fiber/new` 的 env 預設是 nil，不是繼承

所以 fiber 裡 `(dyn :任何東西)` 一律拿到 `nil`，直到你給它一張表。
`fiber/new` 的第三個參數就是 env，不必另外 `fiber/setenv`。
（`ev/spawn` 不一樣，它**會**繼承。）

### `%q` 會把中文變成跳脫位元組

`(string/format "%q" "中文")` => `"\xE4\xB8\xAD\xE6\x96\x87"`，多行也會被壓成一行。
`%v`、`%p` 都一樣，只有 `%s` 讀得出來。所以 `task/brief` 用 `%q`（準確、單行、
不會有歧義），驅動程式遇到字串值改走 `task/headline` ＋ `%s`。

### `=` 對 array 是身分比較

`(= (map inc [1 2]) @[2 3])` 是 **false**。測試要用 `deep=`。

### 定義順序有意義

`runtime.janet` 裡 `host` 那張表的閉包引用 `submit`／`stop`／…，而 Janet 是編譯期
查 symbol，引用還沒定義的名字會直接編不過。所以 `host` 和公開建構子 `new` 排在最後。

## 擴充：一次 Unix 呼叫就是一個 list

```janet
(unix/call :argv   ["/bin/ls" "-la" "/tmp"]
           :cwd    "/home/me"
           :stdin  "/run/aos/42/stdin"
           :stdout "/run/aos/42/stdout"
           :stderr "/run/aos/42/stderr"
           :status "/run/aos/42/status"
           :caller "agent-7")          # 可選，不透明標籤
```

每個欄位都是字串或字串陣列，所以整個呼叫是**純資料**。三條串流不是把位元組塞進
form，而是給位址、由執行者去寫——所以 loop 全程阻塞也不會跟子行程互卡（管線會）。

`:status` 不是「一個裝著整數的檔案」，因為整數表達不了 launch-error 和 signalled，
而且「檔案不存在 = 還不知道」只有在寫入是**原子**的時候才成立。所以內容是一張
Janet table 字面值，寫法是先寫 `.tmp` 再 `rename`。

★ **這是擴充，不是核心。** 核心完全不認識 process，要有這個能力得明講：

```janet
(import ./aos/unix :as unix)
(def rt (runtime/new))
(unix/install (rt :env))
```

### ⚠ 純 Janet 版有一個解不開的歧義

```
sh -c 'exit 137'    os/proc-wait => 137
sh -c 'kill -9 $$'  os/proc-wait => 137
```

`core/process` 身上只有 `:return-code`，原始的 wait status 在 Janet 這層拿不到。
所以這一版誠實地標成 `{:status :ambiguous :code 137 :maybe-signal 9}` 而不是猜。

**這正是 C++ 版存在的理由**：`waitpid` 的 `WIFSIGNALED` 分得清清楚楚，
所以 [`plugin/`](plugin/README.md) 那版沒有 `:ambiguous` 這個狀態。
兩邊都有測試守著同一組案例。

## 測試

```sh
jpm test            # 六支，各自可以單獨跑：janet test/runtime.janet
cd plugin && make test   # C++ 版：起一台真的 daemon，47 項檢查
```

| 測試 | 涵蓋 |
|---|---|
| `task` | 狀態機、終局判定、★ `:paused` 一定要留著 fiber |
| `queue` | FIFO、★ close 之後前面排的還取得到、哨兵不會跟使用者的 table 搞混 |
| `evaluate` | 三種出錯都變成資料、yield／resume、信號、★ 任務裡的真 IO 要跑得完 |
| `env` | 兩張 env 互不相干、不污染 parent、host 少給 key 就少注入 |
| `runtime` | 跨任務累積、★ 爛任務炸不掉 loop、stop 的語義、`aos/*`、兩台各自獨立 |
| `unix` | 驗證、證據檔、cwd、★ exit 137 與 SIGKILL 的結論必須一樣（誠實）、起不來也是結果 |

## 這一層不包含什麼

- **沒有 Unix socket、沒有 CLI**。核心只認得已經 parse 好的 form。
  傳輸層在 [`plugin/`](plugin/README.md)——那一層由 aos-core 提供。
- **沒有多 loop**。一個 runtime 一條 loop。具名多 loop 在 `plugin/`。
- **沒有排程**。`:paused` 的任務不會自動排回隊尾，要自己 `aos/resume`。
  自動輪替（yield = 讓出、排到隊尾）只要三行就能加，但那是排程政策，不是核心。
- **沒有持久化**。env 和任務表都在記憶體裡，行程結束就沒了。
- **沒有安全沙箱**。見上面〈那張 env〉。
- **沒有併發**。見上面〈一次只跑一個〉。
