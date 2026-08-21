# 怎麼讀懂 aos-core

全部 2738 行，扣掉註解**約 2200 行程式**。分五段讀，每段都有一個「讀完應該能回答」
的問題——答不出來就別往下走，因為後面每一段都建立在前一段上。

估計總共 6～8 小時，不必一次讀完。

---

## 第 0 段：先跑，不要先讀（20 分鐘）

**這一段不能跳過。** 整個設計是繞著「串流」長出來的，沒有親眼看過它動，
後面所有的取捨看起來都會像多餘的複雜度。

```bash
make && make test
```

然後開兩個終端。左邊：

```bash
./bin/aos-daemon
```

右邊，一個一個試，每個都想一下「如果是我會怎麼實作」：

```bash
./bin/aos ping                       # 最短的一次完整往返
./bin/aos help                       # 命令樹
./bin/aos daemon status              # 跑兩次，看 served 累加 → 它真的常駐

# ① 邊來邊出：字是一個一個到的，不是最後一起出現
( for w in 這 是 串 流; do printf %s $w; sleep 0.5; done ) | ./bin/aos echo

# ② 上面那個還在跑的時候，另開一個終端打 ping —— 立刻回應，沒有被卡住
./bin/aos ping

# ③ 20 MiB 穿過去，而單一訊框上限是 8 MiB
head -c 20971520 /dev/urandom > /tmp/big.bin
./bin/aos echo < /tmp/big.bin | cksum ; cksum < /tmp/big.bin
```

**要回答的問題**：① 和 ② 分別排除了哪一種偷懶的實作？

<details>
<summary>答案</summary>

① 排除了「daemon 先把 stdin 收完再處理」。② 排除了「daemon 一次只服務一條連線」。
③ 排除了「把整份資料放進一個訊框」。這三件事就是後面所有設計的來源。
</details>

---

## 第 1 段：一個概念（30 分鐘）

只讀一個檔：**[`include/aos/plugin.h`](include/aos/plugin.h)**（143 行，一半是註解）。

它是對外的契約，所以是寫給不熟這個專案的人看的——當成散文讀，不要當成 API 表查。

讀完你會知道：一個命令看得到的世界只有**三件套**（stdin/stdout/stderr、argv、
exit status），沒有第四樣東西。

接著讀 **[`src/daemon.h`](src/daemon.h)** 開頭那段（30 行註解，11 行程式），
它回答第 0 段留下的問題：為什麼是「一條連線一條執行緒 + 全部阻塞」，
而不是事件迴圈。

**要回答的問題**：為什麼選阻塞 IO 反而讓程式變簡單？

---

## 第 2 段：跟著一次 `aos ping` 走一圈（2 小時）

這是最重要的一段。**照順序讀**，這是資料流的順序：

| # | 檔案 | 程式行數 | 看什麼 |
|---|---|---|---|
| 1 | `src/main_cli.c` | 17 | 組裝而已。注意 `argc` 可能是 0 |
| 2 | `src/client.c` | 131 | 連線 → 送 request → **開一條執行緒灌 stdin** → 收訊框 |
| 3 | `src/protocol.h` | 48 | 先讀開頭的格式說明，那是規格 |
| 4 | `src/protocol.c` | 203 | `aos_encode_request` / `aos_decode_request` |
| 5 | `src/frame.c` | 107 | 一次讀／寫一個訊框。全部阻塞 |
| 6 | `src/daemon.c` | — | **只看 accept 迴圈**，收工那半先跳過 |
| 7 | `src/connection.c` | 58 | **一條連線的一生。這是整個專案的脊椎** |
| 8 | `src/registry.c` | 159 | 命令表、`resolve_command`、`handle_command` |
| 9 | `src/commands/ping.c` | 10 | 終點 |

實際要讀的大約 400 行。**建議動手**：在 `connection.c` 的每一步插一行 `fprintf`，
跑一次 `aos ping`，看訊框的順序。

**要回答的問題**：`aos daemon status` 這種**子命令**，是在哪一行被拆成
「命令路徑」和「參數」的？

---

## 第 3 段：串流（1.5 小時）

同一條路，但這次跟著 `aos echo`。

先讀 **[`src/session.c`](src/session.c)**（156 行）——特別是 `pending` 那個緩衝，
以及 `aos_read_input` **為什麼不為了湊滿呼叫端的 buffer 而多讀一塊**
（那會讓「邊來邊出」變成「等湊滿才出」）。

再回頭看 `src/client.c` 的 `forward_stdin`，現在「為什麼 CLI 要兩條執行緒」
應該很明顯了。

然後讀 **[`test/session_test.c`](test/session_test.c)** 的第一個測試。
它把「交錯」寫成了一個可執行的斷言：

```c
AOS_CHECK(strcmp(p.log, "送出|one|送出|two|送出|three|") == 0);
```

**測試是規格的可執行版本**，這個專案裡好幾個微妙的行為都只在測試裡被釘死。

**要回答的問題**：`connection.c` 為什麼在回 exit 之前要呼叫 `aos_drain_input()`？
拿掉會怎樣？（提示：`aos ping < 大檔案`）

---

## 第 4 段：生命週期與併發（2 小時，最難）

到這裡為止都是單一資料流，這一段開始有「同時」。

讀 **[`src/runtime.h`](src/runtime.h)**（39 行程式、41 行註解）再讀
`runtime.c`（219 行）。重點三個：

1. **為什麼這裡有鎖**，而 C++ 協程版沒有
2. `aos_worker_sleep_ms` **為什麼要可以被打斷**（用 `sleep()` 會怎樣）
3. 收工不是「砍掉」，是「停止接受新連線」

然後回到 `src/daemon.c` 讀收工那半段。三個地方值得慢慢看，它們都是被工具逼出來的：

- **self-pipe**：為什麼 signal handler 裡不能鎖 mutex
- **`listen_fd` 只有一個擁有者**：thread sanitizer 抓到的真 race，註解裡有寫
- **等不到就什麼都不拆**：還有連線在跑時 `dlclose` 會發生什麼事

**要回答的問題**：`aos daemon stop` 是在**哪條執行緒**上執行的？
這件事為什麼會導致一個 race？

---

## 第 5 段：擴充（1 小時）

三條路，共用同一份契約：

| | 讀哪裡 |
|---|---|
| 內建命令 | `src/registry.c` 的兩張表 + `src/commands/*.c` |
| 編進來的模組 | `src/modules.c` + `examples/module-cpp/greet.cpp` |
| 外掛 `.so` | `src/plugin.c` + `examples/plugin-hello/hello.c` |

關鍵是看出**三條路最後都走進 `aos_registry_add()`**，而且用的是同一個
`aos_command` 結構。內建的沒有特權。

C++ 那條額外讀 [`include/aos/plugin.hpp`](include/aos/plugin.hpp)：
為什麼例外不能穿過 C 邊界。

---

## 讀完之後：三個練習

看懂和會改是兩回事。這三個由易到難：

### 一、加一個命令（10 分鐘）

照 `src/commands/ping.c` 抄一個 `hello.c`，三步驟見
[`src/commands/builtin.h`](src/commands/builtin.h) 開頭。

### 二、加一條背景執行緒（15 分鐘）

在 `daemon.c` 標了「要加背景常駐工作就加在這裡」的位置加一個 worker，
讓它每 5 秒印一行。然後 `Ctrl-C`——確認 daemon **立刻**結束而不是等 5 秒。

### 三、故意弄壞，看哪個測試會抓到（1 小時，最有價值）

這個練習比前兩個都值得做：它告訴你**每一段程式是為了防什麼而存在的**。
一次改一項，跑對應的指令，看完再改回來。

| 故意改壞 | 跑什麼 | 學到 |
|---|---|---|
| `connection.c` 拿掉 `aos_drain_input()` | `make test` | 為什麼要吃掉沒讀完的 stdin |
| `echo.c` 改成先全部讀完再一次寫出 | `make test` | 串流不是自然發生的，是寫出來的 |
| `client.c` 不開執行緒，改成先灌完 stdin 再收輸出 | 手動跑第 0 段的 ① | 為什麼要全雙工 |
| `session.c` 拿掉 `write_mutex` | `make sanitize SAN=thread` | 為什麼共用 fd 要鎖 |
| `protocol.c` 拿掉 `argc > 65536` 那個檢查 | `make test` | 為什麼不能相信線上來的長度 |
| `daemon.c` 的 stop hook 改回直接 `close(listen_fd)` | `make sanitize SAN=thread` | 那個真實存在過的 race |

---

## 一些讀法上的提示

- **註解裡寫「為什麼」的才是重點。** 「做什麼」看程式就好，會特地寫成中文註解的
  幾乎都是「這裡看起來可以更簡單，但不行，因為……」。
- **測試是規格。** 微妙的行為（交錯、drain、exit code）都釘在測試裡，
  程式改壞了測試會講話。
- **`git log` 是決策紀錄。** 每個 commit 訊息都寫了那次改動在解決什麼問題、
  為什麼選這條路。想知道某段程式為什麼長這樣，`git log -p` 那個檔通常有答案。
- **不要從 `daemon.c` 開始讀。** 它是最大的檔（258 行程式），而且它的每一段都
  預設你已經懂了前面那些。它應該是第 2 段和第 4 段各讀一半。
