# 課程：十二個 Stage

每個 Stage 都是「可編譯、可跑測試」的狀態。不要同時開兩個工地。

每個 Stage 的格式：
**目標** / **要寫什麼** / **驗收** / **★ 這一步的陷阱** / **導師提示**

---

## Stage 0 — 建置與第一個測試

**目標**：`make test` 跑得起來，而且會在失敗時真的失敗。

**要寫什麼**
- `Makefile`：`-std=c++26 -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wconversion`
- `test/check.hpp`：最小的檢查工具（`ok(cond, name)`、`eq(a, b, name)`、`report()`）
- `test/test_core.cpp`：一個一定會過、一個故意寫錯的檢查，確認兩邊都對

**驗收**：故意寫錯的那個會讓 `make test` 回非 0。確認完再把它改對。

**★ 陷阱**

- `先講` **不要用 `assert`。** `NDEBUG` 下它會整個消失，Release 建置的測試會變成
  空跑還回報通過。自己寫一個永遠會執行的。

**導師提示**：這一步很短，但**一定要真的看到紅色**再往下。沒驗證過的測試框架
等於沒有測試。順便讓他加 `make strict`（`-Werror`）和 `make sanitize`
（ASan+UBSan，以及 `SAN=thread`）——後面會救他很多次。

---

## Stage 1 — `Call` 與 `validate()`

**目標**：一次呼叫的固定形狀，加上純函式驗證。

**要寫什麼**

```cpp
struct Call {
    std::vector<std::string> argv;
    std::string stdin_path, stdout_path, stderr_path;   // ★ 名字見陷阱
    std::string exit_path;
    std::string cwd;
    std::optional<std::string> env;    // 可選：exec 前要 source 的檔
    std::optional<std::string> user;   // 可選：誰發起的
};

std::expected<void, std::string> validate(const Call &);
```

**形狀是固定的**：沒有 `_ver`、沒有 `_type`。一個 `Call` 拿在手上就是完整的。

`validate` 只看**形狀**，不碰檔案系統：argv 非空、每項不含 NUL、五個路徑是絕對
路徑且非空。「路徑存不存在」不歸它管。

**驗收**：一組表驅動的測試，每個欄位的相對路徑與空字串都被拒絕，
而且錯誤訊息說得出是哪個欄位。

**★ 陷阱**

- `先撞` **`stdin`／`stdout`／`stderr` 不能當成員名**——`<cstdio>` 裡它們是巨集。
  他會拿到一團看不懂的編譯錯誤。撞到再解釋，很好記。
- `先講` **可選欄位用 `std::optional`，不要用空字串代表「沒給」。**
  空字串是一個合法的 `user`，把「沒給」和「給了空的」混成一件事，之後一定有人踩到。
- `先講` `argv[0]` **不必是路徑**。它可能是絕對路徑、PATH 裡的名字、或純粹的
  路由鍵（Stage 5 會用到）。所以共通驗證只擋空字串。

**導師提示**：這是他第一次用 `std::expected`。`std::unexpected("原因")` 的寫法
可以直接告訴他。這一步應該很快就過，讓他嘗到節奏。

---

## Stage 2 — `Queue`

**目標**：FIFO，多生產者，**收工時排在前面的要做完**。

**要寫什麼**：`push` / `take`（擋住）/ `poll`（不擋）/ `close` / `depth`。
`mutex` + `condition_variable` + `std::deque`。

**驗收**
- FIFO 順序
- `close()` 之後，**已經排著的還取得到**，取完才回「收工」
- `close()` 之後 `push` 回 false

**★ 陷阱**

- `先講` **「收工」不等於「把待辦丟掉」。** 這是兩件事，這裡只做前者。
  做法是往隊尾放一個**哨兵**，不是設一個「關了」的旗標就直接讓 `take` 回空。
- `先撞` **收工之後重複 `take()` 必須都回「收工了」。** 只有第一個呼叫者拿到的話，
  Stage 6 的 N 條 worker 會有 N-1 條永遠卡住。讓他在 Stage 6 撞到再回來修——
  那個症狀（程式停住不動）非常有教育性。
  - 提示：哨兵取出來之後**放回去**。

**導師提示**：哨兵怎麼表達可以讓他自己想。`std::optional<Call>` 當元素、
`nullopt` 當哨兵是個乾淨的解（哨兵不可能跟任何真的 `Call` 撞在一起）。
如果他想用旗標，讓他試，然後問他「排在前面的怎麼辦」。

---

## Stage 3 — `Loop`、`Executor`、`Outcome`

**目標**：那五行核心，以及「結論」的詞彙。

**要寫什麼**

```cpp
enum class Status { Rejected, Exited, Signalled, LaunchError, Unknown };

struct Outcome {
    Status status = Status::Unknown;
    int code = 0, signal = 0, err = 0;
    std::string reason;
};

class Executor { public: virtual Outcome run(const Call &) = 0; };

class Loop {
    void run();     // 擋著取，取到哨兵才回
    bool step();    // 取一個做一個，不擋
    std::size_t drain();
};
```

核心 loop 就是：

```
取一個 -> 取到哨兵就 break -> 交給 executor
```

**驗收**
- 一個丟例外的假 executor，`drain()` **不會**炸掉，而且結論是 `Unknown`
- 前一個失敗，下一個照跑
- `run()` 靠 `close()` 才回得來

**★ 陷阱**

- `先講` **五個 Status 各自只代表一件事。** 尤其
  `Rejected`（什麼都沒發生）vs `LaunchError`（試過了）vs `Unknown`（不知道）。
  ⚠ 這三個之後每一步都會用到，現在把語義定死。
- `先講` **`Unknown` 不是失敗。** 呼叫端不可以把它當失敗，更不可以據此自動重跑
  ——可能已經做過了。
- `先講` Executor 丟例外要接住轉成 `Unknown`，**但那是防呆不是設計**。
  要在介面註解寫清楚「實作不該丟例外」，因為一次真的執行被記成「不知道」，
  後果跟失敗完全不同。

**導師提示**：Executor 介面是整套的關鍵縫。問他一句：
**「為什麼 exec 要關在介面後面，而不是直接寫在 loop 裡？」**
答案是「exec 是唯一有外部作用、唯一難測、唯一跟平台綁死的一段」——
關起來之後上面那幾行可以在完全沒有子行程的情況下測完。

---

## Stage 4 — `ExecExecutor`（★ 最大的一步，分五小步）

**目標**：真的跑一個程式，留下說得清楚的證據。

⚠ **一定要拆開做。** 一次做完會debug到懷疑人生。

### 4a — fork / exec / 三條串流走檔案

順序：`validate → 開 stdin/stdout/stderr → fork → chdir → execve → waitpid`。

**★ 陷阱**
- `先講` **三條串流走檔案，不走管線。** loop 是全程阻塞的，管線的話一個輸出量大的
  子行程會塞滿 buffer 然後跟你互等。走檔案這整類問題不存在。
- `先講` **fork 之後、exec 之前只能用 async-signal-safe 的東西**
  （`dup2`／`chdir`／`execve`／`write`／`_exit`）。所以 argv、envp 這種要配置
  記憶體的事全部在 fork 之前備好。
- `先講` **`chdir` 在子行程裡做**，不是父行程 `os/cd`——後者會改掉整個服務的 cwd。
- `先講` ★★ **fork 前要 `fflush(nullptr)`**。子行程會拿到 parent 沒沖出去的 stdio
  buffer 副本，而它下一步就 `dup2` 把 fd 1 指到**證據檔**——parent 積著的位元組
  會落進去，看起來像子行程印的。
  ⚠ 一般建置**看不到**（`_exit` 不沖 stdio），只有 sanitizer 建置會現形。
  這就是為什麼 Stage 0 要準備 `make sanitize`。

### 4b — self-pipe：分辨 exec 失敗與「程式回 127」

**★ 陷阱**
- `先講` ★★ 沒有這個機制的話，「執行檔不存在」跟「程式自己 `exit(127)`」
  **長得一模一樣**。
  做法：子行程 exec 前開一條 `CLOEXEC` 管線，exec 成功寫端自動關閉、父行程讀到
  EOF；失敗就把 `errno` 寫進去。

**驗收**：`/no/such/binary` 是 `LaunchError`，`sh -c 'exit 127'` 是 `Exited{127}`。

### 4c — exit 檔：原子 ＋ 耐久

exit 檔**只寫一個數字**：`exited N` → `N`，`signalled S` → `128+S`，
`launch-error` → `127`（找不到）／`126`（其他），`rejected` → `125`，
`unknown` → **不寫**。

**★ 陷阱**
- `先講` ★★ **`waitpid` 的原始 status 才分得出 `exit 137` 和 `SIGKILL`**
  （`0x8900` vs `0x0009`）。用 `WIFEXITED`／`WIFSIGNALED`，不要只看合成數字。
  ⚠ 這是這整個專案最重要的一件事之一。
- `先講` ★★ **寫入要三段：`fsync(檔案)` → `rename` → `fsync(目錄)`。**
  三件事擋不同災難：rename 擋「讀到半個檔」（行程崩潰）、fsync 檔案擋「斷電後
  存在但是空的」、fsync 目錄擋「斷電後 rename 整個沒發生」。
  **順序不能反**：先 fsync 再 rename，反了會留下空檔。
  ⚠ 第三步最常被忘記——fsync 檔案**不會**順便保證目錄項目。
- `先講` **執行前要先 `unlink` exit 檔。** 因為輸出是截斷覆寫的，不先清的話
  「檔案存在」不再代表「這一次跑完了」。
  清掉之後就換回這個性質：**exit 檔不在 = 還不知道；在 = 這一次的結論**。

### 4d — 環境：乾淨的預設 ＋ source env 檔

**★ 陷阱**
- `先講` ★★ **預設環境是乾淨的，不繼承本行程的。** 繼承的話同一個 `Call` 在不同
  呼叫端手上會跑出不同結果，證據就變成「在某台機器某個 shell 底下」才成立。
  裡面只放 `PATH`。
- `先講` `env` 檔的做法是「開一個短命的 `sh` 去 source 它、把結果環境倒出來，
  我們拿到之後自己 `execve`」，**不是** `sh -c '. env; exec prog'`。
  後者會讓 shell 成為 exec 目標的人，exec 失敗時是 shell 回 127——4b 好不容易
  做出來的區分就沒了。
  ⚠ 代價：只有環境變數帶得過去，`ulimit`／`umask` 不會。這是真的二選一。
- `先講` 那個 `sh` 的環境傾印後面要印一個**哨兵**。如果 env 檔裡有 `exit 0`，
  `sh` 會在 `env -0` 之前結束 → 「退出碼 0 ＋ 輸出是空的」→ 你會拿一份**空環境**
  去跑程式。有哨兵才分得出「環境是空的」和「傾印不完整」。
- `先講` env 檔的 stdout 要導到 stderr，stdin 要給 `/dev/null`
  （不然它會偷吃這次呼叫的 stdin）。

### 4e — PATH 查找

**★ 陷阱**
- `先講` **不要用 `execvp`／`execvpe`。** `execvp` 查找時用的是**呼叫端 `environ`
  的 PATH**，而你要用的是「這次呼叫最終那份 envp」的 PATH。自己寫，約 30 行。
- `先講` errno 照 POSIX 收斂：過程中有人回 `EACCES` 就報 `EACCES`（→126），
  全程 `ENOENT` 才報 `ENOENT`（→127）。

**Stage 4 整體驗收**：`exit 0` / `exit 7` / `exit 137` / `kill -9` / `kill -15` /
執行檔不存在 / 沒有執行權限 / cwd 不存在 / stdin 餵得進去 / cwd 生效且沒改到自己的
/ 含空白與中文的參數原樣傳過去。

**導師提示**：這一步最容易挫折。每個小步都要他跑完測試再往下。
`exit 137` vs `kill -9` 那組測試寫出來的時候，讓他**先猜**兩者會不會一樣，
再跑。那一刻是整個課程的高點。

---

## Stage 5 — `Router`

**目標**：依 `argv[0]` 分流到不同的 queue。

**驗收**：命中 / 沒命中走 fallback / 沒 fallback 就 `NoRoute` / 目的地收工 `Closed`。

**★ 陷阱**
- `先講` **整串精確比對**，不取 basename。自動猜 basename 的話
  `/opt/evil/llm-ask` 也會被路由走。
- `先講` `NoRoute` 與 `Closed` **不可以安靜忽略**——那代表沒有人會處理這通呼叫，
  等它的人會永遠等。
- `先講` 分流要在**進 queue 之前**，不是在 Executor 裡。因為分流的目的就是
  換一條 loop：一個慢的 `llm-ask` 跟一般 exec 擠同一條 loop 會把後面全部卡住。

**導師提示**：這裡順便讓他做 `DropArgv0Executor`——路由鍵要被吃掉，
不然 `exec /bin/ls` 會去找一支叫 `exec` 的執行檔。

---

## Stage 6 — `Pool`

**目標**：N 條 worker 共吃一個 queue，上限就是 worker 數量。

**用 `std::jthread`**：自動 join，而且 `stop_token` 是內建的。

**驗收**：`peak_in_flight() == N`（真的用滿，沒退化成一次一個），
而且總時間符合併發（12 個 × 0.15 秒 < 1.2 秒，序列化要 1.8 秒）。

**★ 陷阱**
- `先撞` ★★ **這裡會撞到 Stage 2 那個「收工後重複 take」的問題。**
  症狀是收工時卡住不動。讓他自己 debug 到那裡——這是全課程最好的一課。
- `先講` **一條 worker 一個 `Loop` 實例**，共用 queue 與 executor。
  `Loop` 的 stats 不是 atomic 的，共用一個會 race。
- `先講` observer 要由 Pool 上鎖串起來，寫 observer 的人才不必想併發。
- `先講` 記 peak 要用 compare-exchange 迴圈，不是 `if (now > peak) peak = now`
  ——後者在兩條 worker 同時進來時會互相蓋掉。

**導師提示**：這一步**一定要跑 `make sanitize SAN=thread`**。

---

## Stage 7 — `Channel` 介面 ＋ `PriorityQueue`

**目標**：讓各條 loop 挑自己的排序政策。

**★ 陷阱**
- `先講` **`Call` 裡不加 priority 欄位。** 那是各條 loop 的政策，不是呼叫的形狀。
  `PriorityQueue` 收一個「怎麼算優先值」的函式。（這是總規矩第四條的第一次應用。）
- `先講` **同分必須嚴格 FIFO。** `std::priority_queue` 對同分沒有保證，
  那會讓同優先權的順序不可重現、有人餓死而且重現不了。用一個遞增序號當第二鍵。
- `先撞` **排序容器裡不能放哨兵**——它會被優先值排到不知道哪去，
  「排在前面的做完才停」就不成立了。這裡要改用旗標＋`notify_all`。
  ⚠ 這跟 Stage 2 的結論相反，讓他自己發現為什麼。

**導師提示**：`Router` 要改吃 `Sink`、`Loop`／`Pool` 改吃 `Source`。
這是他第一次為了「讓別人能替換」而抽介面——問他為什麼不直接讓 `Queue` 支援優先權。

---

## Stage 8 — 逾時與輸出上限

**目標**：一個不結束的子行程不能永遠佔住一條 worker。

`ExecExecutor` 收一個 policy 函式（跟 Stage 7 同一招）。

**★ 陷阱**
- `先講` ★★ **砍的是行程群組，不是那一個 pid。** 子行程自己開的孫行程不會跟著死，
  它們被 init 收養後在背景一直跑。工作「被砍了」但機器上還有東西在動，最難查。
  子行程一 fork 就 `setpgid(0,0)`，父行程也做一次（避免競態），砍的時候 `kill(-pgid, …)`。
- `先講` **先 SIGTERM、等寬限、再 SIGKILL**，讓子行程有機會自己收尾。
  ⚠ 就算它收到 TERM 之後自己乾淨 `exit(0)`，**還是要記成逾時**。
- `先講` `timed_out`／`output_capped` 是 `Outcome` 上的**獨立旗標**，不是新 Status。
  否則被你砍掉跟「外面有人砍它」都是 `Signalled{SIGKILL}`，分不開——
  而這兩件事後續差很多。
- `先講` **不限的時候走阻塞 `waitpid`**，零輪詢成本。不用這功能的人不該付代價。

exit 檔：逾時 → `124`（`timeout(1)` 的慣例），爆量 → `123`（沒有標準，自己定的）。

**導師提示**：孫行程那個測試值得寫——起一個會生孫的命令，砍掉之後確認孫也死了。

---

## Stage 9 — `WorkTracker` ＋ 收工

**目標**：收工前先確認系統真的靜下來。

**★ 陷阱**
- `先講` ★★ **只看 queue 空不空是錯的**，兩個理由：(1) queue 空了不代表沒事做，
  某條 worker 正拿著一個 Call；(2) **處理途中會生出新工作**（Stage 11 會很明顯）。
  所以數的是「**進了系統還沒出去的有幾個**」：`push` +1、處理完 -1。
- `先講` 兩個順序不能反：`push` 要**先 +1 再 push**；observer 包裝要**先跑使用者的
  observer 再 -1**（observer 可能會推新工作）。

**導師提示**：讓他寫一個 A→B 的鏈測試：A 做完推 B，然後斷言
「A 的 queue 空了，但計數還是 1」。那一項把這個 Stage 的意義講完了。

---

## Stage 10 — 外部輸入

**目標**：從函式庫變成跑得起來的服務。

三小步：JSON parser → NDJSON（stdin）→ Unix socket。

**★ 陷阱**
- `先講` parser **只支援平面 object，值只能是字串或字串陣列**。數字、布林、null、
  巢狀一律**拒絕**。嚴格是設計不是偷懶：`Call` 的每個欄位本來就只有這兩種。
- `先講` **不認得的 key 要拒絕，不是忽略。** `{"usr":"me"}` 安靜地少一個 `user`
  是那種很久以後才發現的錯。
- `先講` `\uXXXX` **要處理 surrogate pair**，不然 emoji 會壞掉。路徑和 user 都可能
  有非 ASCII。
- `先講` **一行一個物件**。價值不是好寫，是「一個壞掉的請求傷不到下一個」——
  串流式 parser 碰到壞語法就不知道下一個物件從哪開始。
- `先講` socket 是「**收下就回**」：`{"ok":true}` 只代表排進去了，client 自己輪詢
  exit 檔。★ 這讓 loop 的形狀完全不用動。
- `先講` socket 權限 **0600**，而且是 **bind 前收緊 umask**，不是 bind 後 chmod
  ——後者中間有窗口，而這條 socket 可以叫起任意命令。
- `先講` 信號處理常式裡只能做 async-signal-safe 的事。停止服務要用 **self-pipe**
  （寫一個 byte），不能在那裡 `close(listen_fd)`——會跟 `accept` 競態。

**導師提示**：JSON parser 那段是純粹的細活，讓他一次寫完再測。
socket 那段先讓他用 `python3` 寫個小 client 手動打，看到 `{"ok":true}` 再寫測試。

---

## Stage 11 — agent 擴充

**目標**：第一個真擴充，**核心一行都不改**。

只用三個公開的縫：`Executor`、`Sink`、`Loop::Observer`。

**★ 陷阱**
- `先講` ★★ 直覺寫法 `while (!done) { dispatch(); wait(); }` **不行**：
  `wait` 佔住 worker 會跟下游死鎖，而且一個跑三天的 Round 不可能靠一個 C++ 堆疊活著。
  所以 Round 的進度**存在磁碟上**，一次 tick 只做一小步，派完就返回。
  child 做完之後由下游的 observer 把它叫醒。**沒有任何人在等任何人。**
- `先講` ★★★ **先存進度，再派工。** 反過來的話 push 成功但存檔前崩潰，重開會再派
  一次，而那個 child 可能已經跑過了——外部作用發生兩次。
  反方向的失敗是安全的（停住、看起來像「還不知道」）。**停住比重複好。**
- `先講` **child 的證據路徑由派工的那一層統一填**，不讓 controller 填。
  總規矩第三條這樣才不可能被寫錯。
- `先講` agent **不認識 LLM**。prompt、tool schema 全在 `Controller` 介面後面。
  模型呼叫本身就是一個普通的 `exec` Call（包成 curl-like adapter）。

**導師提示**：這一步是前面所有規矩的收成。做完問他：
**「哪一條規矩如果當初沒定，這一步會壞掉？」**（答案：全部五條。）

---

## 之後

課程走完就跟 `../aos-simple/` 功能對齊了。接下來的四項在
[../../aos-simple/docs/TODO.md](../../aos-simple/docs/TODO.md)：
`Unknown` 之後的政策、搶佔、Task tree、監看目錄。
