# aos-lisp 外掛 —— C++ 入口，Janet 當嵌入式腳本

把 Janet 掛進 aos-core。**aos-core 一行都不用改**：這是 `.so` 外掛，只用
`AOS_PLUGIN_ABI_VERSION 1` 那個穩定介面。

```
aos lisp eval agent-7 '(+ 1 2)'
  │
  └─ CLI ──UDS──> aos-daemon
                    └─ 連線執行緒（aos-core 的）
                         ├─ 找到／開一條叫 agent-7 的 loop
                         ├─ 把 job 排進它的 queue
                         └─ 擋著等
                              ↕  只交換位元組
                       [loop 執行緒：自己的 Janet VM + 自己的長存 env]
                         └─ 求值 → 填 job → notify
  ←── 3
```

## 馬上試

```sh
make                                    # build/aos-lisp.so
make test                               # 起一台真的 daemon 打它（47 項檢查）

AOS_PLUGINS=$PWD/build/aos-lisp.so ../../aos-core/bin/aos-daemon &
../../aos-core/bin/aos lisp eval agent-7 '(def x 7)'
../../aos-core/bin/aos lisp eval agent-7 '(* x 6)'      # => 42
../../aos-core/bin/aos lisp eval build   'x'            # => 錯，那是另一張 env
../../aos-core/bin/aos lisp loops
```

## 三個命令

| | |
|---|---|
| `aos lisp eval <loop> <form…>` | 求值。沒給 form 就從 stdin 讀。輸出是值，exit status 0／1 |
| `aos lisp loops` | 目前有哪些 loop、各做過幾個、排隊中幾個 |
| `aos lisp stop <loop>` | 叫一條 loop 收工（排在前面的做完才停） |

## 分界：C++ 管什麼、Janet 管什麼

| | 誰 | 檔 |
|---|---|---|
| 入口、命令樹、CLI、協定 | aos-core | （不用改） |
| 分派、queue、執行緒、loop 生命週期 | C++ | `loop.cpp` |
| fork／exec／waitpid、證據檔 | C++ | `unixcall.cpp` |
| VM 生命週期、C↔Janet 交接 | C++ | `vm.cpp` |
| **求值語意**：sigmask、隔離 env、錯誤怎麼變成資料 | Janet | `bootstrap.hpp` |

`loop.cpp` 完全不認識 Janet，`plugin.cpp` 也是。Janet 只出現在 `vm.cpp` 與
`unixcall.cpp`，而 `bootstrap.hpp` 是一段 Janet 原始碼。

## ★ 為什麼一條 loop 一條執行緒

因為 **Janet 的 VM 狀態是 thread-local**（`janet.h` 把 `JANET_THREAD_LOCAL`
定成 `__thread`）。一條執行緒 `janet_init()` 一次，就是一個完全獨立的 VM。實測：

```
loop 0: mine=loop-0 core-env=0x7f0f70010880
loop 1: mine=loop-1 core-env=0x7f0f68010880
loop 2: mine=loop-2 core-env=0x7f0f60010880
```

三條 pthread、三個 VM、三個不同的 core env，互不污染。這正好就是 aos-core
「一條連線一條執行緒、全部阻塞」的同一個模型，只是這裡的單位是 loop 不是連線。

推論出兩條**不可以違反**的規矩：

1. **連線執行緒絕對不碰任何 `janet_*`。** 它跟 loop 之間只透過 `Job` 交換位元組
   （`job.hpp`）。把介面收斂成「進去一段原始碼、回來兩段輸出加一個 exit status」，
   這條規矩就從「要記得遵守」變成「想違反也沒有型別可用」。
2. **`Vm` 只能在自己那條執行緒的堆疊上建立與解構。** 所以它不是 `Loop` 的成員，
   是 `Loop::body()` 裡的區域變數。在 A 執行緒 init、B 執行緒 deinit 是 UB。

## ★ 為什麼具名、動態建立

因為每條 loop 有自己的長存 env。固定 N 條 worker ＋ round-robin 的話，任務落到
哪條不確定，env 就不能拿來存狀態——而長存 env 正是這整套的重點。具名之後
`agent-7` 這個名字就對應到一張確定的 env。

`aos lisp eval <名字>` 找不到就開一條。`aos lisp stop <名字>` 之後同名再用是
**全新的 env**（有測試守著）。

## ★★ 搬到 C++ 換到了什麼：exit 137 vs SIGKILL

這是整個 pivot 最具體的理由。純 Janet 版分不出這兩件事：

```
sh -c 'exit 137'    os/proc-wait => 137
sh -c 'kill -9 $$'  os/proc-wait => 137
```

`core/process` 身上只有 `:return-code`，原始的 wait status 在 Janet 這層拿不到
（POSIX shell 的 `$?` 也一樣是 128+sig）。所以純 Janet 版被迫發明一個
`:ambiguous` 狀態來誠實地說「我不知道」。

`waitpid` 的原始 status 分得清清楚楚：

| | raw | `WIFEXITED` | `WIFSIGNALED` |
|---|---|---|---|
| `exit 137` | `0x8900` | 1，code 137 | 0 |
| `kill -9` | `0x0009` | 0 | 1，sig 9 |

所以這一版**沒有 `:ambiguous`**，只有三個都確定的結論：

```janet
{:status :exited       :code 137}
{:status :signalled    :signal 9 :core-dumped false}
{:status :launch-error :errno 2 :message "No such file or directory"}
```

順帶修好第二件 Janet 做不到的事：**分辨 exec 失敗和「程式跑了但回 127」**。
做法是經典的 self-pipe——子行程在 exec 前開一條 `CLOEXEC` 管線，exec 成功就自動
關閉、父行程讀到 EOF；失敗就把 `errno` 寫進去。沒有這個機制的話「執行檔不存在」
跟「shell 回 127」長得一模一樣。兩者都有測試守著。

## unix/call

```janet
(unix/call :argv   ["/bin/ls" "-la" "/tmp"]
           :cwd    "/home/me"
           :stdin  "/run/aos/42/stdin"
           :stdout "/run/aos/42/stdout"
           :stderr "/run/aos/42/stderr"
           :status "/run/aos/42/status"
           :caller "agent-7"        # 可選，不透明標籤
           :env    {"PATH" "/bin"}) # 可選，給了就整套換掉
```

三條串流走**檔案**而不是管線。loop 是一次跑一個、全程阻塞的，管線的話一個輸出量
大的子行程會塞滿 buffer 然後跟我們互等，整條 loop 停住。走檔案這一整類問題不存在，
所以下面可以老實 `fork` 完就 `waitpid`，不必開讀取執行緒。（aos-core 的 CLI 要開
兩條執行緒、daemon 端 stdin 要做成拉的，就是在付這個代價。）

`:argv[0]` 一定要絕對路徑，**不走 shell、不走 PATH**：走 PATH 的話「實際跑了哪支
執行檔」取決於當下的環境變數，那不是證據。

### status 檔為什麼不是「一個裝著整數的檔案」

1. 整數表達不了 launch-error 和 signalled；
2. 「檔案不存在」要能可靠地代表「還不知道」，那只有寫入是**原子**的時候才成立。

所以內容是一張 Janet table 字面值（`parse` 讀得回來，零相依），寫法是先寫
`<status>.tmp` 再 `rename`。**檔案在 = 結果已知；檔案不在 = 還不知道，不可以當成失敗。**
`(unix/read-status path)` 對不存在的檔回 `nil` 就是這個意思。

還有一個免費的兩階段標記：status 檔不在的時候，**stdout 檔在不在**分得出
「根本沒起來」和「可能跑過了」——後者絕對不可以自動重跑。

### 不覆蓋既有證據

`:stdout`／`:stderr`／`:status` 如果已經是**普通檔案**就拒絕執行。裝置檔
（`/dev/null`）不受限制——這裡看的是 `st_mode` 不是比對路徑字串，所以不必開特例。

## 幾個踩過的坑

| 症狀 | 原因 |
|---|---|
| 任務裡 `unknown symbol unix/call` | 用了 `janet_cfuns` 而不是 **`janet_cfuns_prefix`**。前者的 regprefix 只用在 marshal 登記表上，綁定名是光禿禿的 `call` |
| 同上，但 cfun 明明裝好了 | `(make-env)` 的預設 parent 是 `root-env`，**不是** `janet_core_env()` 回的那張表。要明寫 `(make-env (curenv))` |
| bootstrap 報 `splice can only be used in…` | Janet 的註解是 `#`，`;` 是 **splice 運算子** |
| 任務裡的 IO 永遠不完成，也不報錯 | sigmask 寫成 `:a`。ev 用 user9(await)／user8(interrupt) 實作阻塞 IO，`:a` 把它們一起擋了。要 `:ey01234567` |
| bootstrap 回的函式下一次 GC 就沒了 | Janet 那側沒有任何綁定引用得到它（那是刻意的隔離），所以 C 這邊一定要 `janet_gcroot` |
| `-Wpedantic` 對 janet.h 叫一堆 | 它用了 flexible array member。用 `-isystem` 引入，不要 `-I` |
| 測試永遠不結束 | 光禿禿的 `wait` 會連背景的 daemon 一起等 |

## 收工

`aos_plugin.shutdown` 在所有連線都結束之後、`dlclose` 之前被呼叫，那裡把所有 loop
執行緒 `request_stop()` ＋ join。**一定要 join**：loop 不是 aos-core 的連線，
`aos_runtime_active_connections` 數不到它們，沒有別人會等；而 `dlclose` 之後這些
執行緒的程式碼就從記憶體消失了。

> ⚠ 已知限制：某條 loop 手上如果有一個永遠不返回的任務，daemon 的收工會卡在這裡。
> loop 只在**兩個 job 之間**看得到哨兵。這跟 aos-core 警告「不要用 `sleep(60)`
> 而要用 `aos_worker_sleep_ms`」是同一類問題。

## 這一層不包含什麼

- **不串流。** 值是等任務跑完才一次寫回呼叫端的。要串流就得讓 loop 執行緒去碰
  session，而那會把「連線執行緒獨佔 session」這條規矩打破。
- **沒有 resume。** 任務 `yield` 會變成一則乾淨的錯誤，fiber 直接丟棄。
  （仍然要在 sigmask 裡擋 `:y`——不擋的話 yield 會穿過 `resume` 炸掉整條 loop。）
- **沒有持久化。** loop 的 env 在記憶體裡，daemon 一停就沒了。
- **沒有安全沙箱。** 任務叫得動 `unix/call`、`os/*`、`file/*`。跟 aos-core 一樣，
  安全邊界是那條 `0600` 的 socket。
