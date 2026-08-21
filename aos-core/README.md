# aos-core

`aos` 是薄型命令列入口；`aos-daemon` 是真正處理命令的常駐程式。
兩者以 Unix Domain Socket 通訊。純 C99，**零第三方相依**。

```text
Shell → aos（CLI）→ Unix Domain Socket → aos-daemon → 命令樹
                                             ├─ 內建命令
                                             ├─ 編進來的模組（可以是 C++）
                                             └─ 外掛（.so，執行期載入）
```

CLI 不解析業務命令。以下呼叫會把三個參數逐項送給 daemon：

```bash
aos new "bot alpha" --verbose
```

傳送內容是 `new`、`bot alpha`、`--verbose`，不是重新拼接的 shell 字串，
所以空白、引號與空字串的參數邊界都能保留。

> 第一次讀這個專案？[**READING.md**](READING.md) 有一條分五段的閱讀路線，
> 從「先跑起來看它做什麼」開始，不是從程式碼開始。

## 一個命令看得到的世界就是三件套

| 三件套 | 在程式裡是什麼 |
|---|---|
| stdin／stdout／stderr | `aos_read_input()`／`aos_write_output()`／`aos_write_error()` |
| argv | `run()` 的 `argc`、`argv`（已經去掉命令路徑） |
| exit status | `run()` 的回傳值 |

就這樣，跟一個 Linux 程式看到的一樣。**內建命令和外掛命令用的是完全相同的介面**，
所以外掛做得到的事永遠不會比內建的少。

## 併發模型：一條連線一條執行緒，全部阻塞

這是整個專案最重要的一個決定。

沒有事件迴圈、沒有狀態機、沒有回呼地獄。daemon 每接到一條連線就開一條執行緒，
那條執行緒從頭到尾用阻塞 IO 把事情做完。所以命令可以這樣寫：

```c
while ((state = aos_read_input(session, chunk, sizeof chunk, &got)) == AOS_OK) {
    aos_write_output(session, chunk, got);
}
```

想擋多久就擋多久，不會影響別的連線。要打 HTTP 就直接 `curl_easy_perform()`，
不必為了不卡住事件迴圈去拆狀態機——**這是相對於協程版最大的簡化**。

代價是任何跨連線的共用狀態都要鎖，那些集中在 [`src/runtime.h`](src/runtime.h)。

## 串流是真的

```bash
printf 'hello\n' | aos echo            # pipe
aos echo < big.bin > copy.bin          # 檔案重導向，20 MiB 也沒問題
tail -f server.log | aos echo          # 邊來邊出
```

CLI 用兩條執行緒（一條灌 stdin、一條收輸出），所以 **stdin 還沒結束就看得到輸出**。
daemon 端是用拉的（命令呼叫 `aos_read_input()` 才去拿下一塊），
所以 **stdin 沒有總量上限**，只有單一訊框 8 MiB 的限制。

## 三種擴充方式

### 一、加一個內建命令

1. 在 `src/commands/` 加一個 `.c`，寫一個這個形狀的函式：
   ```c
   int aos_cmd_mine(aos_session *s, int argc, const char *const *argv, void *user);
   ```
2. 在 `src/commands/builtin.h` 加一行宣告
3. 在 `src/registry.c` 的表裡加一列

Makefile 用 wildcard 掃 `src/`，新檔案不必手動加進建置。
子命令就是多開一張子表、掛到分組節點的 `children`。

### 二、加一條背景執行緒

想要定時清理、看門狗、批次佇列這類跟任何一次呼叫都無關的東西：

```c
static void tick(aos_runtime *rt, aos_worker *self, void *user) {
    while (!aos_worker_sleep_ms(self, 60000)) {   /* 回 1 = 該收工了 */
        do_the_periodic_thing(rt);
    }
}
/* 加在 src/daemon.c 標了「要加背景常駐工作就加在這裡」的地方 */
aos_worker_spawn(runtime, "tick", tick, NULL);
```

**一定要用 `aos_worker_sleep_ms` 而不是 `sleep()`**。它是可被打斷的：收工時立刻醒來。
用 `sleep(60)` 的話 daemon 收工要等最多 60 秒，而且很難聯想到是這裡造成的。
有一項測試專門守著這件事。

### 三、把子專案編進來（可以用 C++）

**跟外掛用的是同一份契約**——同一個 `aos_plugin` 結構、同一張命令表、同一個
`aos_host`。差別只有「怎麼載入」，所以之後想從編進來改成 `.so`，程式碼一行都不用動。

|  | 編進來 | 外掛（.so）|
|---|---|---|
| 改了要 | 重編 aos-core | 換一個檔 |
| ABI 錯配 | 不可能（一起編）| 要對版本 |
| 適合 | 一起開發的子專案、還在動的介面 | 獨立、穩定的能力 |

加一個：寫一個 `aos_plugin_init` 形狀的函式 → 在 `src/modules.c` 的表加一行 →
把原始檔加進 `MODULE_SRCS`。

```bash
make MODULE_SRCS=examples/module-cpp/greet.cpp
make test-module     # 這條路自己的測試
```

**用 C++ 寫的話一定要 include [`aos/plugin.hpp`](include/aos/plugin.hpp)。**
它解決一件事：

> **例外絕對不能穿過 C 邊界。** 那是 UB，實務上就是整個 daemon 消失。
> 而且爆炸的不會是你寫的那行，是別人的 C 迴圈，第一眼完全看不出關聯。

這件事沒辦法靠自律，因為會丟的多半不是你寫的那行——是 `std::stoi`、是
`vector::at`、是 `new`。所以把 run 包進 `aos::guarded<>`：

```cpp
static const aos_command commands[] = {
    {"greet", "打招呼", aos::guarded<greet>, nullptr, nullptr, 0},
    //                  ^^^^^^^^^^^^^^^^^^ 例外會變成 exit 1 + 一行 stderr
};
```

`MODULE_SRCS` 是空的時候 daemon 連 libstdc++ 都不會連進去，保持純 C。

### 四、寫一個外掛（.so）

不用改 aos 一行程式，也不用重編它。只要 include 一個標頭：

```c
#include "aos/plugin.h"

static const aos_host *host;

static int shout(aos_session *s, int argc, const char *const *argv, void *user) {
    for (int i = 0; i < argc; ++i) {
        host->write_output(s, argv[i], strlen(argv[i]));
    }
    return 0;
}
static const aos_command commands[] = {{"shout", "喊回去", shout, NULL, NULL, 0}};
static const aos_plugin plugin = {AOS_PLUGIN_ABI_VERSION, "mine", commands, 1, NULL};

const aos_plugin *aos_plugin_init(const aos_host *given) {
    host = given;
    return &plugin;
}
```

```bash
gcc -shared -fPIC -Iaos-core/include -o mine.so mine.c
AOS_PLUGINS=$PWD/mine.so aos-daemon &
aos shout 你好
```

外掛從哪裡找：環境變數 `AOS_PLUGINS`（用 `:` 隔開，跟 PATH 一樣），
以及 `$XDG_CONFIG_HOME/aos/plugins/` 底下所有的 `.so`。

**一個壞掉的外掛不會讓 daemon 起不來**——只會在 stderr 記一行然後跳過。
用 `aos daemon plugins` 看載到了哪些。

完整的 ABI 說明在 [`include/aos/plugin.h`](include/aos/plugin.h)，
可跑的範例在 [`examples/plugin-hello/`](examples/plugin-hello/)。

## 命令是一棵樹

```bash
aos help              # 列出所有命令
aos ping              # → pong
aos daemon            # 列出 daemon 這一組的子命令
aos daemon status     # uptime、服務過的請求數、處理中的連線數
aos daemon plugins    # 載入了哪些外掛
aos daemon stop       # 做完手上的事之後收工
```

## 建置與執行

沒有相依套件，clone 下來就能建。用 Makefile 而不是 CMake，
因為這一層沒有任何要偵測的東西——CMake 最擅長的事一件都用不到：

```bash
make            # bin/aos、bin/aos-daemon、範例外掛
make test       # 全部測試
make clean
```

交叉編譯就照一般的方式覆寫：

```bash
make CC=arm-linux-gnueabihf-gcc
```

clangd／VS Code 要的 `compile_commands.json` 用 `bear -- make` 產生。
（`bear` 不是建置相依，只有要編輯器補全時才需要。）

先在一個終端啟動 daemon，再從另一個終端呼叫 CLI：

```bash
./bin/aos-daemon
./bin/aos ping
```

## Socket 路徑

兩個程式使用相同規則決定 socket 路徑：

1. 環境變數 `AOS_SOCKET`
2. `$XDG_RUNTIME_DIR/aos.sock`
3. `/tmp/aos-<uid>.sock`

socket 檔權限是 `0600`——這條 socket 可以叫起任意命令，權限開大等於把 shell
送給同機的其他使用者。

## 程式分工

| 路徑 | 負責 |
|---|---|
| `include/aos/plugin.h` | **對外的全部承諾**：外掛唯一要 include 的標頭 |
| `include/aos/plugin.hpp` | C++ 用的一層薄包裝，主要是攔住例外 |
| `src/buf.c` | 會長大的位元組緩衝（不是字串，可含 `\0`）|
| `src/protocol.c` | 線上格式：訊框標頭、request／exit 的編解碼。純函式 |
| `src/frame.c` | 在一個 fd 上阻塞讀寫一個訊框 |
| `src/session.c` | 三條串流；也是交給外掛的那張函式表 |
| `src/registry.c` | 命令樹：那兩張表、解析、派發、help 渲染 |
| `src/runtime.c` | 跨呼叫存活的狀態 + 背景工作 + 收工 |
| `src/plugin.c` | 外掛的載入端（dlopen、ABI 檢查、接進命令樹）|
| `src/modules.c` | **編進來的模組**：那張你要編輯的表 |
| `src/daemon.c` | 監聽、accept 迴圈、一連線一執行緒 |
| `src/connection.c` | 一條連線的一生 |
| `src/client.c` | CLI：兩條執行緒的全雙工 |

## 測試

```bash
make test          # protocol、session、end_to_end
make test-module   # 編進來的 C++ 模組那條路
```

改完之後值得再跑這三個。它們不是每次建置的成本，是「動過就跑一次」：

```bash
make strict        # 一整排嚴格警告（-Wconversion 那些）
make analyze       # GCC 靜態分析器（-fanalyzer）
make sanitize             # ASan + UBSan，真的跑測試
make sanitize SAN=thread  # ThreadSanitizer —— 併發改動一定要跑這個
```

目前這五個都是零警告、零報告。**thread sanitizer 抓到過一個真的 race**
（`aos daemon stop` 在連線的執行緒上關掉 accept 迴圈的 listen fd），
所以動到執行緒的話別跳過它。

| 測試 | 涵蓋 |
|---|---|
| `protocol` | 純函式：編解碼來回、各種壞輸入、`buf` 的二進位安全與成長 |
| `session` | 真 socketpair：`echo` 真的是邊讀邊寫、小 buffer 不掉資料、`ping` 不碰 stdin、大寫入自動切塊、**背景工作的 sleep 可被打斷** |
| `end_to_end` | 真的起 daemon 用 CLI 打它：21 項檢查，含 20 MiB 串流、含 NUL 的二進位、16 條並行連線、外掛、收工 |
| `make test-module` | 編進來的 C++ 模組那條路：11 項檢查，重點是**連丟 20 次例外 daemon 不能倒** |

測試用 `test/check.h` 而不是 `assert`，因為 `assert` 在 `NDEBUG` 下會整個消失，
Release 建置的測試會變成空跑還回報通過。

## 幾個踩過的坑

- **SIGPIPE 要擋**。client 中途離開時寫進斷掉的 socket 會發 SIGPIPE，
  預設行為是殺掉行程——不擋的話 client 按個 Ctrl-C 就能讓整個 daemon 消失。
- **一切都是 (指標, 長度)**，沒有一處靠 NUL 結尾。payload 可以含 `\0`，有測試守著。
- **訊框長度不含 kind 那個位元組**，所以長度 0 是合法的（`stdin_end` 就是）。
- **回 exit 之前要把沒讀完的 stdin 吃掉**，否則對方下一次 write 會拿到 EPIPE，
  看起來像命令失敗了，其實只是我們太早收線。
- **stdin 是終端機就別等 EOF**，不然 `aos ping` 會停在那裡等一個不會來的 Ctrl-D。
- **`_POSIX_C_SOURCE` 要開**。`-std=c99`（不是 `gnu99`）預設把 POSIX 藏起來，
  症狀是 `strdup`／`clock_gettime`／pthread 全部「宣告不到」。

## 這個專案不包含什麼

LLM 呼叫、工具設定檔那一層**不在這裡**，它會是一個獨立專案，以外掛的形式接上來。
這個 repo 只負責：協定、daemon、CLI、命令樹、外掛載入。
