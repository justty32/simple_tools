# aos-simple

一個迴圈，從 queue 取出一個**固定形狀**的 struct，驗一下，交出去。C++17，
**零第三方相依**。

```cpp
for (;;) {
    auto call = queue.take();   // 取一個，空的就擋著等
    if (!call) break;           // 取到哨兵 = 收工
    handle(*call);              // 驗證 → 交給執行者
}
```

## 現在做到哪

struct、queue、loop、驗證、exec、**依 argv[0] 分流**、**有上限的併發**，
以及第一個擴充：[**agent Round loop**](ext/agent/README.md)。
還沒有任何輸入來源——佇列只能由程式直接 `push`。

```sh
make test        # core 108 + exec 107 + agent 47
make strict      # -Werror
make sanitize             # ASan + UBSan
make sanitize SAN=thread  # ThreadSanitizer
```

四個都是零警告、零報告。

## 文檔

| | |
|---|---|
| [**`docs/OVERVIEW.md`**](docs/OVERVIEW.md) | **★ 先讀這份**：長出了什麼、為什麼。給「記憶還停在單純 loop」的人 |
| [`docs/ADDING-A-LOOP.md`](docs/ADDING-A-LOOP.md) | **怎麼加一條新 loop、loop 之間怎麼傳 `Call`**。要在這上面長東西就讀這份 |
| [`docs/TODO.md`](docs/TODO.md) | 還沒做的，以及為什麼／怎麼做 |
| [`ext/agent/README.md`](ext/agent/README.md) | **擴充**：AOS 的 Agent Round loop。核心不認識它 |
| 〈[外部輸入](#外部輸入ndjson)〉 | 一行一個 JSON 物件，走 stdin |

★ **動過 `src/exec.cpp` 就跑一次 `make sanitize`。** 有一整類 bug（見下面
〈fork 前要 fflush〉）只有那一輪抓得到，一般建置會假性通過。

## 一次呼叫

```cpp
struct Call {
    std::vector<std::string> argv;

    std::string stdin_path;    // ★ 不能叫 stdin──那是 <cstdio> 的巨集
    std::string stdout_path;
    std::string stderr_path;

    std::string exit_path;
    std::string cwd;

    std::optional<std::string> env;    // exec 前要 source 的檔
    std::optional<std::string> user;   // 誰發起的
};
```

除了 `argv` 和 `user`，其他全部是**路徑**。三條串流不是位元組，是位址——
由具體執行者去讀去寫。

`user` 是不透明標示符：表示這個請求來自於誰。不是路徑、不驗證、不保證唯一。

`argv[0]` **不必是路徑**。它可能是：

- 一個絕對路徑 → 直接跑
- 一個裸名字（`sh`）→ 走 PATH 查找
- 一個**路由鍵**（`exec`、`llm-ask`）→ 根本不會被 exec，只決定去哪條 loop

所以「`argv[0]` 該長什麼樣」是各個目的地自己的事，共通驗證只擋空字串。

### 形狀是固定的，沒有 `_ver` 也沒有 `_type`

所以沒有「這個版本支不支援」那條分支，也沒有「依 type 去啃剩下的 key」那條分支。
一個 `Call` 拿在手上就是完整的，不必再問它是什麼。

之後真的需要第二種呼叫（python 之類），那是**再開一個型別**的事，不是在這個結構
裡加欄位然後到處 `if`。這一版的簡單就建立在這件事上。

## 檔案

| | 負責 | 碰外面？ |
|---|---|---|
| [`src/call.hpp`](src/call.hpp) | 固定的 struct ＋ `validate()` | 否，**純函式** |
| [`src/channel.hpp`](src/channel.hpp) | queue 的介面：`Sink`／`Source`／`Channel` | 否 |
| [`src/queue.hpp`](src/queue.hpp) | FIFO，多生產者多消費者，哨兵收工 | 否 |
| [`src/priority_queue.hpp`](src/priority_queue.hpp) | 會排序的 Channel，優先值由 loop 自己給 | 否 |
| [`src/loop.hpp`](src/loop.hpp) | 核心 loop ＋ `Executor` 這個縫 | 否 |
| [`src/router.hpp`](src/router.hpp) | 看 `argv[0]` 決定進哪個 queue | 否 |
| [`src/pool.hpp`](src/pool.hpp) | N 條 worker 共吃一個 queue | 否 |
| [`src/exec.hpp`](src/exec.hpp) | 驗證 → source env → fork/exec/waitpid → 寫 exit | **是，全部在這裡** |
| [`ext/agent/`](ext/agent/README.md) | **擴充**：Agent Round loop。只用 `Executor`／`Sink`／`Observer` 三個縫 | 是（round 狀態） |
| [`ext/input/`](ext/input/ndjson.hpp) | **擴充**：NDJSON -> `Call`。不碰 loop、不碰 exec | 否 |
| [`bin/main.cpp`](bin/main.cpp) | 只有接線，沒有邏輯 | — |

## 外部輸入（NDJSON）

一行一個 JSON 物件，走 **stdin** 或 **Unix socket**（`AOS_LISTEN=1`）。
兩條路共用同一個 dispatch，差別只在有沒有回覆。

```json
{"argv":["exec","/bin/ls","-la"],"stdin":"/r/1/in","stdout":"/r/1/out",
 "stderr":"/r/1/err","exit":"/r/1/exit","cwd":"/tmp","user":"agent-7"}
```

| JSON key | 對到 | |
|---|---|---|
| `argv` | `argv` | 必填，字串陣列 |
| `stdin` `stdout` `stderr` `exit` `cwd` | 五個路徑 | 必填 |
| `env` `user` | 同名 | 可選 |

★ 沒有 `_ver`、沒有 `_type`——形狀是固定的。

**★ 一行一個的價值不是好寫，是「一個壞掉的請求傷不到下一個」。** 串流式 parser
碰到壞語法之後就不知道下一個物件從哪開始，只能整條中斷；一行一個的話壞的那行
報掉、下一行照常。這跟「一個爛任務炸不掉 loop」是同一條規矩。

**★★ 不認得的 key 一律拒絕，不是忽略。** `{"usr":"me"}` 會報錯而不是安靜地少一個
`user`——打錯字被安靜吞掉，是那種要很久以後才會發現的錯。同理，值只能是字串或
字串陣列，數字／布林／null／巢狀一律拒絕。

**★ 呼叫端怎麼知道做完了：輪詢 `exit` 檔。** 不在 = 還不知道，在 = 這一次的結論。
走 stdin 的好處就是沒有連線要回應，所以不必決定「連線是等到做完還是收下就回」
那個會往回影響 loop 形狀的問題。之後接 socket 的時候再面對它。

空行與 `#` 開頭的行會跳過（方便手寫測試檔），CRLF 也讀得動。

### socket：★ 收下就回

```
client ──▶ {"argv":["exec","/bin/ls"], …}
server ──▶ {"ok":true}          ← 這時候命令才剛進 queue，還沒跑
```

一行請求對一行回覆，**照順序**——所以不需要 request id。`{"ok":false,"error":"…"}`
會帶回拒絕的原因。

⚠ `{"ok":true}` 只代表**排進去了**。命令本身的成敗在它自己的 `exit` 檔裡。

★ 這個選擇讓 loop 的形狀**完全不用動**：連線執行緒只做 parse 與 dispatch，
跟 stdin 那條路一模一樣，沒有任何從 `Outcome` 回到連線的路徑。
（「等到做完」那條路才會需要那個回頭路，並且要處理「連線先斷了怎麼辦」。）

路徑規則跟 aos-core 一致：`AOS_SOCKET` → `$XDG_RUNTIME_DIR/aos-simple.sock`
→ `/tmp/aos-simple-<uid>.sock`。權限 **0600**——這條 socket 可以叫起任意命令，
開大等於把 shell 送給同機的其他使用者。（★ 是 bind **前**先收緊 umask，
不是 bind 後才 chmod：後者中間有個窗口。）

一條連線一條執行緒，全程阻塞——因為「收下就回」很快，不需要事件迴圈。

除了 exec 之外一個子行程都不起、一個檔案都不開。測試也照這條線分成兩支
（`test-core` / `test-exec`），所以「核心不碰外面」不是一句口號，是跑得出來的。

## 分流：`argv[0]` 決定去哪條 loop

```cpp
Queue exec_queue;
ExecExecutor raw;
DropArgv0Executor gated{raw};          // 把路由鍵吃掉
Pool pool{exec_queue, gated, 4};       // ★ 同時最多四個

Router router;
router.route("exec", exec_queue);
// ★ 刻意不設 fallback ── 最初入口預設關閉
```

```
aos:  exec /bin/ls -la     ->  exec_queue（四併發）  ->  真的跑 /bin/ls -la
aos:  llm-ask 問題         ->  llm_queue（自己的節奏）
aos:  /bin/ls -la          ->  NoRoute，什麼都不會發生
```

**為什麼分流要在 queue 之前而不是在 Executor 裡**：分流的目的就是換一條 loop。
`llm-ask` 那種東西有自己的節奏（等網路、限流），跟一般 exec 擠同一條 loop 的話，
一個慢的 `llm-ask` 會把後面所有命令一起卡住——loop 是一次跑一個的。

**比對是整串精確比對**，不取 basename、不做前綴。`/opt/somewhere/llm-ask`
**不會**自動命中——要它命中就把那個字串也註冊上去。自動猜 basename 的話
`/opt/evil/llm-ask` 也會被路由走。

**路由鍵會被吃掉**：`DropArgv0Executor` 把 `argv[0]` 拿掉才交給下一層，
否則 `ExecExecutor` 會傻傻地去找一支叫 `exec` 的執行檔。只送 `exec` 沒帶命令
的話，吃掉之後 argv 空了 → `Rejected`，正是我們要的錯誤。

**沒地方去要講出來**：`dispatch()` 回 `NoRoute`（沒命中又沒 fallback）或
`Closed`（目的地收工了），不會安靜地把呼叫吞掉——吞掉的話呼叫端會等一個
永遠不會被處理的呼叫。

## 有上限的併發

一般的 `Loop` 一次跑一個，那對「跑外部程式」太保守：一支跑 30 秒的命令會擋住
所有人，而它 29.9 秒都只是在等。但也不能無上限——exec 會真的開行程、開檔案。

所以是 `Pool`：**上限就是 worker 數量**，用結構保證，不是靠計數器去檢查。

可以直接共吃一個 `Queue`，是因為 `Queue::take()` **取到哨兵會放回去**——
N 條 worker 都看得到「收工了」，不會有人永遠擋著。那個當初為了「重複 take
不要卡住」寫的行為，在這裡正好就是收工廣播。

> ⚠ `Executor` 會被多條執行緒同時呼叫。`ExecExecutor` 沒有狀態所以沒問題；
> 自己寫的要自己管好共用狀態。observer 由 `Pool` 上鎖串起來，所以寫 observer
> 的人不必操心併發（代價是 observer 裡做慢事會互相拖）。

測試裡守著兩件事：`peak_in_flight() == 4`（真的用滿，沒退化成一次一個）
與 12 個 ×0.15 秒的總時間 < 1.2 秒（序列化要 1.8 秒）。

## `validate()` 擋什麼

只看形狀，**不碰檔案系統**（「這個路徑存不存在」屬於執行者）：

- `argv` 不能是空的，每一項不能含 NUL（`execve` 用 NUL 結尾傳參數，帶不過去）
- `argv[0]` 只擋空字串——**它不必是路徑**，見上面
- 五個路徑欄位都要絕對路徑、非空、不含 NUL
- `env` 給了的話比照路徑；`user` 給了的話只擋 NUL

拒絕的理由會講出是哪個欄位。

## 五個結論，各自只代表一件事

```cpp
enum class Status {
    Rejected,     // 驗證沒過，**根本沒送去執行**
    Exited,       // 跑完了，code 有意義
    Signalled,    // 被信號殺死，signal 有意義
    LaunchError,  // 起不來（執行檔不存在、沒權限…）
    Unknown,      // ★ 沒能給出結論
};
```

★ `Rejected` 和 `LaunchError` 的差別是**有沒有發生過事情**：前者保證什麼都沒做，
後者是試過了但沒起來。

★ `Unknown` **不是失敗**。它代表「不知道」，所以不可以當成失敗處理，更不可以據此
自動重跑。目前 `DefaultExecutor` 回的就是它，執行者丟例外的時候也是——那時候我們
不知道它做到哪裡了。

## exec

```
1. validate()             純驗證，還沒有任何外部作用
2. unlink(exit)           ★ 清掉上一次的結論
3. open stdin/out/err     第一個真正的外部作用（O_TRUNC，就是 > 的語義）
4. source env 檔          ★ 排在開檔之後，雜訊才有地方去
5. fork + exec            不經 shell
6. waitpid                拿原始 status
7. 原子＋耐久寫 exit      .tmp -> fsync -> rename -> fsync 目錄
```

`Executor` 是個介面，`ExecExecutor` 是目前唯一的實作。把它獨立出來的理由：那是
整套裡**唯一有外部作用、唯一難測、唯一跟平台綁死**的一段。

> ⚠ 實作**不應該丟例外**。loop 會接住並轉成 `Unknown`，但那會讓一次真的執行看起來
> 像「不知道」，而這兩件事的後果完全不同。有結論就回結論，包含失敗的結論。

### exit 檔：一個數字

| | 數字 |
|---|---|
| `exited N` | `N` |
| `signalled S` | `128+S` |
| `launch-error` | `127`（找不到）／`126`（找得到但起不來） |
| `rejected` | `125` |
| `unknown` | **不寫檔** |

沿用 shell 的既有慣例，`read code < exit` 就拆完了。

> ⚠ 一個數字表達不了「程式自己 `exit(137)`」和「被 SIGKILL 殺死」的差別——兩者
> 都是 137。這是「只寫一個數字」的固有代價，不是實作沒做到。**完整的區分留在
> 回傳的 `Outcome` 裡**（`Status::Exited{code:137}` vs `Status::Signalled{signal:9}`），
> 因為在記憶體裡多帶兩個 int 不花成本。測試裡有一項專門守著「exit 檔裡兩者一樣」——
> 哪天它們不一樣了，代表有人在猜。

### ★ 第 2 步：為什麼要先 unlink

因為輸出是**截斷覆寫**的，所以「檔案存在」本身不再代表「這一次跑完了」——上一次
留下的檔也在那裡。先把 exit 檔 unlink 掉，就把這個性質換回來了：

```
exit 檔不在  =  還沒有結論（還在跑，或我們自己死了）
exit 檔在    =  這一次的結論，而且是完整的（rename 是原子的）
```

而且 stdout 檔在不在還能再細分一次：exit 不在、stdout 也不在 = 連開檔都還沒到；
exit 不在但 stdout 在 = **可能已經跑過了**，絕對不可以自動重跑。

### ★★ 兩件只有拿原始 status 才做得到的事

```
exit 137     raw=0x8900  WIFEXITED=1   WEXITSTATUS=137
kill -9      raw=0x0009  WIFSIGNALED=1 WTERMSIG=9
```

只看「128+signal」那個合成數字的話這兩件事一模一樣。（隔壁
[`aos-lisp`](../aos-lisp/README.md) 的純 Janet 版就是卡在這裡，被迫發明一個
`:ambiguous` 狀態。）

第二件是 **exec 失敗要用 self-pipe 偵測**：子行程在 exec 前開一條 `CLOEXEC` 管線，
exec 成功的話寫端自動關閉、父行程讀到 EOF；失敗就把 `errno` 寫進去。沒有這個機制
的話「執行檔不存在」跟「程式自己回 127」長得一模一樣。兩件都有測試守著。

### ★★ 環境：預設是乾淨的

**不繼承本行程的環境。** 繼承的話同一個 `Call` 在不同呼叫端手上會跑出不同結果
（誰的 shell 剛好 export 了什麼就影響誰），證據就變成「在某台機器某個 shell
底下」才成立。乾淨的預設讓結果只取決於 `Call` 本身。

裡面刻意只有一項：

```
PATH=/usr/local/bin:/usr/bin:/bin
```

沒有 `LC_ALL`／`LANG`——什麼都不設的時候 POSIX 的預設就是 C locale，
所以「不設」本身已經是可重現的了。沒有 `HOME`、`TZ`、`USER`、`SHELL`，
要就自己寫進 env 檔。

正因為 PATH 現在由 aos 決定（不是呼叫端的 shell），`argv[0]` 走 PATH 查找
才是可重現的——這就是它不必訂死絕對路徑的原因。

> ★ 查找是自己實作的，不用 `execvp`／`execvpe`。因為 `execvp` 用的是**呼叫端
> `environ` 的 PATH**，而我們要用的是「這次呼叫最終那份 envp」的 PATH。
> errno 照 POSIX 收斂：過程中有人回 `EACCES` 就報 `EACCES`（→126），
> 全程 `ENOENT` 才報 `ENOENT`（→127）。

### ★ env 檔：exec 前先 source

```cpp
c.env = "/etc/aos/profile";   // 可選
```

```sh
# profile
export PATH="$PATH:/opt/aos"
if [ -d /data ]; then export DATA_DIR=/data; fi
export COMPUTED="$(hostname)-worker"
```

是真的 `source`——條件式、命令替換、`$` 展開都能用，不是 `KEY=VALUE` 對照表。

**基底是那份乾淨環境**，所以：

```
沒給 env 檔  =>  乾淨環境
給了 env 檔  =>  乾淨環境 ⊕ 檔案改的部分
```

兩條路徑同一個基底，所以 `export PATH="$PATH:/opt/bin"` 的結果是可預測的。

#### 做法：先 source 再捕捉，自己 execve

```
sh -c '. "$1" >&2 || exit 1; env -0; printf ...' sh <envfile>
        ↓ 捕捉 NUL 分隔的環境
fork + execve(目標, argv, 那份環境)
        ↑ self-pipe 還在，127／126 分得出來
```

★ **不是** `sh -c '. env; exec prog'`。那樣會讓 shell 成為 exec 目標的人，
exec 失敗時是 shell 回 127，跟「程式自己回 127」再次分不開——那正是 self-pipe
好不容易解決的事。現在 `execve` 還是由我們直接發出，所以 `LaunchError` 的區分
完整保留，argv 邊界也不會被 shell 再解析一次。

> ⚠ 代價：**只有環境變數帶得過去**。`ulimit`、`umask`、fd 設定、shell 函式都不會，
> 因為那些是那個短命 `sh` 自己的行程狀態。要那些就得讓 shell 來 exec，
> 那就得放棄上面那個區分——是個真的二選一。

三個細節：

- **env 檔的 stdout 導到 stderr**（`. "$1" >&2`），才不會混進要 parse 的環境傾印裡。
  所以 `echo '載入中…'` 會落到這次呼叫的 **stderr 證據檔**，而不是弄壞環境。
- **env 檔的 stdin 是 `/dev/null`**，不給它碰這次呼叫的 stdin——那些位元組是要給
  目標程式的，被 source 的檔吃掉一段會非常難查。
- ★★ **傾印後面印一個哨兵**。如果 env 檔裡有一句 `exit 0`，那個 `sh` 會在 `env -0`
  之前就結束，於是「退出碼 0 ＋ 輸出是空的」。少了哨兵我們會把空輸出當成
  「環境是空的」，然後拿一份空環境去跑程式——安靜且危險。有哨兵就變成：
  沒看到哨兵 = 這份環境不完整 = 不要用。

env 檔失敗（不存在、語法錯、提早 exit）一律是 `LaunchError`：目標程式一個位元組
都沒跑到，exit 檔是 126，`sh` 的抱怨留在 stderr 證據檔裡。

### ★★ exit 檔的耐久性：三段，缺一不可

```
1. fsync(檔案)   內容真的刷到碟片
2. rename        原子替換
3. fsync(目錄)   「這個名字指向那個 inode」也刷到碟片
```

三件事**擋的是不同的災難**：

| | 少了它會怎樣 |
|---|---|
| `rename` 原子性 | 讀到寫到一半的檔（**行程崩潰**時） |
| `fsync(檔案)` | 斷電後檔案存在但**是空的**──最糟的一種，看起來像有結論 |
| `fsync(目錄)` | 斷電後 rename **整個沒發生**，檔案不存在 |

⚠ 順序也是有意義的：一定要**先 fsync 再 rename**。反過來的話 rename 已經生效
但內容還沒落地，斷電就留下一個空檔。

⚠ 第 3 步最常被忘記：**`fsync` 檔案不會順便保證目錄項目**。

所以 `exit 檔在 = 結論已知` 現在**對斷電也成立**。代價是每次呼叫兩次磁碟同步。
（目錄 fsync 失敗時刻意不回報整體失敗：內容已落地、rename 也成功了，
此刻結論就是「已寫入」；謊稱失敗會讓呼叫端重跑一次已經發生的作用。）

### ★★ 逾時與輸出上限

**機制在核心，政策在各條 loop。** `Call` 裡**沒有** timeout 欄位，也不會有——
那跟 priority 一樣是政策，不是呼叫的形狀。所以 `ExecExecutor` 收一個函式：

```cpp
ExecExecutor exec{[](const Call &c) {
    ExecOptions o;
    o.timeout = std::min(parse_wait_timeout(c.argv), kMyCeiling);  // 自己壓天花板
    o.grace   = std::chrono::milliseconds{5000};
    o.max_output_bytes = 64 << 20;
    return o;
}};
```

`bin/aos-simple` 用環境變數：`AOS_EXEC_TIMEOUT_MS`、`AOS_EXEC_GRACE_MS`、
`AOS_EXEC_MAX_OUTPUT`。**預設都是不限**——通用執行器不該替使用者猜「多久算太久」，
猜錯會砍掉合法的長工作。

| | exit 檔 |
|---|---|
| 逾時被砍 | `124`（`timeout(1)` 的慣例）|
| 輸出爆量被砍 | `123`（⚠ 沒有標準，我們自己定的）|

三個實作細節：

- **★★ 砍的是行程群組，不是那一個 pid。** 子行程自己開的孫行程不會因為 parent
  被砍就跟著死，它們會被 init 收養然後在背景一直跑。這種孤兒最難查：工作「被砍了」
  但機器上還有東西在動。所以子行程一 fork 就 `setpgid` 自成一群。有測試守著。
- **先 SIGTERM，等寬限，再 SIGKILL。** 讓子行程有機會自己收尾（刷 buffer、
  刪暫存檔）。就算它收到 TERM 之後自己乾淨地 `exit(0)`，**還是記成逾時**。
- **不限的時候走阻塞 `waitpid`，零輪詢成本。** 不用這個功能的人不必付任何代價。

★ `timed_out`／`output_capped` 是 `Outcome` 上獨立的旗標，不是新的 `Status`。
沒有它們的話，被我們砍掉跟「外面有人砍它」都是 `Signalled{SIGKILL}`，完全分不開——
而這兩件事的後續差很多（前者該調參數或放棄，後者通常該重跑）。

### ★★ 收工：先等系統靜下來

```cpp
tracker.wait_idle();   // 1. 等真的沒事了
agent_queue.close();   // 2. 才關，順序照資料流
agent_pool.join();
exec_queue.close();
exec_pool.join();
```

**只看 queue 空不空是錯的**，兩個理由：

1. queue 空了不代表沒事做——某條 worker 正拿著一個 Call 在 executor 裡面。
2. ★★ **處理途中會生出新工作。** agent 派 child 給 exec，child 做完又推一個
   tick 回 agent。所以「兩個 queue 同時是空的」那一瞬間，可能只是工作正好在
   兩條 loop 之間的半空中。

所以 [`WorkTracker`](src/tracker.hpp) 數的是「**進了系統還沒出去的有幾個**」：
`push` 成功 +1，executor 處理完 -1。這個算法沒有窗口——處理途中生出來的新工作
是在**父工作 -1 之前**就 +1 的，所以計數不會假性歸零。

⚠ 兩個順序不能反：`TrackedSink` 要**先 +1 再 push**，`wrap` 要**先跑使用者的
observer 再 -1**（observer 可能會推新工作）。兩個都有測試守著。

### ★ fork 前要 fflush

子行程會拿到 parent 那些**還沒寫出去**的 stdio buffer 的副本，而它下一步就是
`dup2` 把 fd 1／2 指到證據檔。所以只要子行程那邊有任何東西沖了 stdio，parent 積著
的位元組就會落進**這次呼叫的 stdout／stderr 檔**，看起來像是子行程印的。

`_exit` 本身不沖 stdio，所以一般建置看不到。實際咬到的是 sanitizer 建置——
ASan／TSan 攔截 `_exit`，它們的 teardown 會沖。同一類的未來地雷還有：把 `_exit`
改成 `exit`、在子行程裡加一句 `perror`、連進來的函式庫裝了 atexit handler。

與其一個一個守，不如在源頭 `fflush(nullptr)`。**這也是為什麼動過 exec 就要跑一次
`make sanitize`**：那一項回歸測試在一般建置下會假性通過。

## 收工：排在前面的做完才停

`queue.close()` 是往隊尾放一個哨兵，不是把還沒做的丟掉。「叫它收工」和「把待辦
丟掉」是兩件事，這裡只做前者。取到哨兵之後**再取還是會拿到「收工」**，不會有人
永遠擋在那裡。

## 這一版不包含什麼

- **監看目錄那條輸入還沒接。**（stdin 與 socket 有了）
- **沒有搶佔。** 優先權只影響還排在 queue 裡的東西；已經在跑的不會讓位。
- **`Unknown` 之後沒有政策。** 不能自動重跑，但也不能就這樣放著。
- **沒有逾時。** 一個不結束的子行程會佔住一條 worker；四個就把 exec 那條路塞滿。
  逾時不在 struct 裡。
- **沒有輸出上限。** 寫多大就多大。
- **沒有搶佔。** 優先權只影響還排在 queue 裡的東西；已經在跑的不會讓位。
- **沒有重試。** `Unknown` 之後誰來重試、怎麼避免重複作用，都還沒設計。
- **env 只帶得走環境變數。** `ulimit`／`umask` 帶不過去，見上面。
