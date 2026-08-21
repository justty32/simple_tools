# aos-cpp 建置規劃

給實作者（codex）的規格。**先讀完整份再動手**，尤其是最後的「陷阱清單」——
裡面每一條都是 aos-c 實際踩過或差點踩到的。

---

## 0. 這是什麼

`aos-cpp` 是 `aos-c` 的後繼者，**新開的 repo，不是遷移**。

`aos-c` 做的事不變：讀一個檔案，裡面是一筆一筆的「行程生成紀錄」，照順序執行。
變的是三件事：

| | aos-c | aos-cpp |
|---|---|---|
| 格式 | 九行文字（八欄位 + 空白分隔行） | **JSON Lines**（一行一個 JSON 物件） |
| I/O | `std::istream` + 三個 streambuf 轉接器 | **POSIX syscall + buffer**，不用 iostream |
| 建置 | 手寫 Makefile，無外部相依 | **vcpkg + CMake**，可以用函式庫 |

`aos-c` 保持原樣不動，不需要相容、不需要轉換工具。舊檔案就留給舊 binary 讀。

### 為什麼換 JSON（唯一的理由，別記錯）

**因為要能加欄位。** 九行格式加一個欄位等於改記錄的行數，所有既有檔案與 reader
全部錯位——那是破壞性升級。JSON 加一個鍵不會動到既有檔案。

不是為了 `_type`，不是為了美觀，不是為了人類好讀（指令檔本來就不該手寫）。
**這份規劃裡不要出現 `_type`。** 目前只有一種記錄型別，預留一個沒人要用的
辨別鍵只會讓每個讀者都問「還有哪些型別」。真的出現第二種再加，JSON 的好處
正是那時候加不會破壞任何東西。

---

## 1. 技術棧

- **語言**：C++17
- **編譯器**：gcc（clang 也要能過，因為 macOS）
- **建置**：CMake ≥ 3.21 + `CMakePresets.json`
- **相依**：vcpkg **manifest 模式**（`vcpkg.json` 進版控）
- **平台**：POSIX only —— Linux / WSL / macOS。
  CMake 遇到 Windows 要**直接 `message(FATAL_ERROR)`**，不要留 `#ifdef` 分支。
  （理由沿用 aos-c：一份永遠不會被執行的實作，是沒有人能查證的宣稱。）

### 相依套件

| vcpkg port | CMake target | 用途 |
|---|---|---|
| `nlohmann-json` | `nlohmann_json::nlohmann_json` | JSON 解析／序列化 |
| `catch2` | `Catch2::Catch2WithMain` | 測試 |

不要再加別的。特別是不要引入 fmt、spdlog、CLI11——這個程式的輸出只有幾行
錯誤訊息，CLI 只有一個選擇性參數。

---

## 2. 記錄格式（最重要的一節）

### 2.1 框架：JSON Lines

一行一筆記錄，每行是一個完整的 JSON 物件，以 `\n` 結尾。

```
{"argv":["echo","hello"]}
{"argv":["sh","-c","sleep 30"],"timeout_ms":5000}
{"argv":["cat"],"stdin":"/etc/hostname","stdout":"/tmp/out","exit":"/tmp/rc"}
```

- **不要**用 JSON 陣列包住整份檔案。JSON Lines 讓記錄邊界自我界定，
  `}` + `\n` 就是分隔——九行格式那個「第 9 行空白分隔」的機制自然被取代。
- 空行**跳過**，不算一筆記錄（方便產生端排版）。
- 寫入端一律輸出 LF。讀取端接受 CRLF（去掉結尾的 CR）。
- 寫入端**不得**輸出縮排或換行美化（那會破壞一行一筆）。

### 2.2 Schema

| 鍵 | 型別 | 必要 | 預設 | 語意 |
|---|---|---|---|---|
| `argv` | string[] | 是 | — | 非空陣列，argv[0] 非空字串。argv[0] 走 PATH（如 execvp） |
| `stdin` | string | | `""` | 空=繼承呼叫端；否則以唯讀開啟 |
| `stdout` | string | | `""` | 空=繼承；否則建檔並截斷（`>` 而非 `>>`） |
| `stderr` | string | | `""` | 同上 |
| `exit` | string | | `""` | 空=丟棄；否則截斷後寫入十進位狀態 + `\n` |
| `cwd` | string | | `""` | 空=繼承 |
| `env` | object(string→string) | | `{}` | 空=純繼承；非空=在繼承的環境上**擴充**（覆寫同名、新增其餘、其他保留） |
| `timeout_ms` | uint | | `0` | `0` = 不限時。否則超過就殺 |

**注意三個相對 aos-c 的變化：**

1. **`extra` 砍掉了。** JSON 本身就有「未知鍵」的概念，不需要一個官方的垃圾桶
   欄位。（`DISC_CONC_1.md` 的待辦本來就列著要砍它。）
2. **`env` 從「`KEY=VALUE` 字串清單」變成「物件」。** 這免費消滅了
   `ENV_ENTRY_MALFORMED` 狀態、`=` 的位置檢查、以及「同名後者勝」的順序規則
   （物件的鍵天生唯一）。**鍵不得為空字串、不得含 `=`**，這兩條仍要檢查。
3. **`timeout_ms` 是新的**，也是這次換格式唯一要換到的東西。詳見 §4。

### 2.3 未知鍵 → **拒絕**，不是忽略

這條反直覺，所以講清楚理由：

如果忽略未知鍵，那麼一個**舊版 binary 讀到帶 `timeout_ms` 的檔案時，會安安靜靜
地不限時執行它**。使用者以為設了逾時，實際上沒有。這正是九行格式那個「錯位之後
仍然語法合法、於是默默執行一筆沒人寫過的指令」的坑，換個樣子重演。

同理，`"stdou"` 打錯一個字，忽略的話就是「重導向默默沒發生」。

**所以：遇到 schema 以外的鍵，回報 `UnknownKey` 並停止整輪。**

這不會傷害「加欄位免費」這個目的：加欄位之後，**既有的檔案仍然合法**（這是相對
九行格式的關鍵勝利），只是新檔案需要新 reader——而它會**大聲**告訴你，不會默默
做錯事。

### 2.4 限制（trust boundary）

指令檔等同可執行程式碼，要當不可信輸入處理。

| 限制 | 值 | 為什麼 |
|---|---|---|
| 單行（單筆記錄）位元組上限 | 預設 1 MiB，可由呼叫端指定 | 擋畸形輸入吃光記憶體 |
| 整份輸入位元組上限 | 預設 64 MiB，可由呼叫端指定 | 因為現在是整份讀進來（見 §3.2） |
| JSON 巢狀深度上限 | **3**，硬性 | schema 最深就是 物件→陣列→字串。**不設就是遞迴爆堆疊 = DoS** |
| `argv` 元素數上限 | 256 | 沿用 aos-c |
| `env` 條目數上限 | 256 | 沿用 aos-c |

深度限制**必須在解析過程中生效**，不能解析完再檢查——那時堆疊已經爆了。
用 `nlohmann::json::parse` 的 parser callback（它會給你 `depth`）或 `sax_parse`。

---

## 3. 架構

### 3.1 分層（沿用 aos-c，這部分已經吵過了，不要重新設計）

```
      run / main          <- 驅動迴圈、CLI、錯誤訊息
          |
    +-----+-----+
  format       exec       <- 這兩層互不相識
    |            |
    +---- inst --+        <- 純資料型別，不知道 bytes 也不知道 fork
```

- `inst` —— `inst_t` 型別本身。不含任何 I/O、不含任何 JSON。
- `format` —— **唯一**知道記錄長什麼樣子的地方。JSON 只能出現在這一層的 `.cpp`。
- `exec` —— **唯一**知道 `fork`/`execvp` 的地方。它只吃 `inst_t`，永遠不吃路徑、
  不吃串流、不吃 buffer。
- `run` —— 迴圈。

### 3.2 讀取模型：整份讀進來，不串流

**這是相對 aos-c 的明確反轉，是刻意的。**

`aos-c` 是串流的（讀一筆→跑一筆→再讀）。`aos-cpp` **先把整份輸入讀進一塊
buffer，全部解析並驗證完，才開始執行第一筆**。

換到的東西：

1. **原子性**——「檔案裡有一筆壞的 → 一筆都不跑」。串流做不到這件事：讀到第 5 筆
   才發現壞掉時，前 4 筆已經執行了，而執行不可逆。
2. 三個 streambuf 轉接器（`FdBuf`／`MemBuf`／`FileBuf`）整個不需要存在。
3. 修掉 aos-c 一個已記錄的無聲 bug：走 stdin 時子行程會吃掉指令串流
   （因為現在 fork 之前就已經讀到 EOF 了，繼承到的 fd 沒東西可吃）。

付出的代價（**要寫進文件，不要假裝沒有**）：

- **失去 FIFO 上的增量執行**。生產端還在寫第 2 筆時，第 1 筆不會先跑。
  「開→寫一批→關」的批次生產端不受影響；長命的 `producer | aos-cpp` 管線會變成
  「等上游做完才一次跑完」。
- 記憶體上界從「最長的一筆」變成「整份輸入」，所以 §2.4 那個整份上限是**必要的**，
  不是裝飾。

### 3.3 I/O 一律走 POSIX syscall

`open` / `read` / `write` / `close`。**不要 `#include <iostream>` 或 `<fstream>`。**
錯誤訊息用 `write(STDERR_FILENO, ...)` 或 `fprintf(stderr, ...)`。

（唯一例外：Catch2 自己會用 iostream，那是測試，無所謂。）

---

## 4. 逾時（`timeout_ms`）

這是這次唯一的新能力，也是最容易做錯的部分。

### 4.1 為什麼要有

`aos-c` 用 `waitpid(pid, &st, 0)`——**無限等待**。一個卡住的子行程會讓整個 aos-c
永遠掛住。配上「一條 FIFO 一次只能有一個 aos-c」的部署紀律，一次卡死就是這條管線
永久停擺。這是實際存在的洞。

### 4.2 實作

**行程群組**（必須，否則只殺得掉直接子行程，孫行程會活下來）：

- 子行程 `fork` 後**第一件事**就是 `setpgid(0, 0)`。
- 父行程在 `fork` 之後**也**呼叫 `setpgid(pid, pid)`，忽略 `EACCES`。
  兩邊都做是標準做法，用來消除競爭（誰先跑不確定）。
- 殺的時候殺整個群組：`kill(-pid, SIG...)`。

**等待迴圈**（`timeout_ms > 0` 時）：

```
1. waitpid(pid, &st, WNOHANG)  -> 已結束就直接收工
2. 沒結束 -> 睡一小段，累計經過時間，回到 1
3. 超過 timeout_ms -> kill(-pid, SIGTERM)，給寬限期 kTimeoutGraceMs = 2000ms
4. 寬限期內還沒死 -> kill(-pid, SIGKILL)，然後阻塞式 waitpid
```

- 時間用 `clock_gettime(CLOCK_MONOTONIC, ...)`（**不要用 wall clock**，會被調時間影響）。
- 睡眠用 `nanosleep`，間隔從 1ms 開始遞增、上限 50ms。
  （固定 10ms 會讓每個快指令都多付 10ms；固定 1ms 會空轉。遞增兩邊都顧到。）
- `timeout_ms == 0` 時走原本的阻塞式 `waitpid`，一行分支就好，不要為了統一而讓
  不限時的情況也去輪詢。

### 4.3 回報

被殺掉的子行程本來就會落進既有的 `128 + signal` 規則：
`SIGTERM` → 143，`SIGKILL` → 137。**不需要新的 exit code。**

但 `ExecResult` 要加一個 `bool timed_out`，因為 143 分不出「我殺的」和
「別人殺的」。

---

## 5. 專案結構

```
aos-cpp/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── README.md
├── .gitignore
├── include/aos/
│   ├── export.h          # AOS_API 可見性巨集
│   ├── inst.hpp          # inst_t + InstState
│   ├── format.hpp        # read/write（buffer 進、buffer 出）
│   ├── exec.hpp          # execute + ExecResult/ExecState
│   └── aos.h             # C ABI（純 C，不含任何 C++ 標頭）
├── src/
│   ├── inst.cpp
│   ├── format.cpp        # <- 唯一可以 #include <nlohmann/json.hpp> 的地方
│   ├── exec.cpp
│   ├── capi.cpp
│   ├── run.cpp / run.hpp
│   └── main.cpp
├── tests/
│   ├── test_format_read.cpp
│   ├── test_format_write.cpp
│   ├── test_format_limits.cpp     # 深度、大小、argv/env 數量、未知鍵
│   ├── test_format_malformed.cpp  # 對抗性輸入
│   ├── test_exec.cpp
│   ├── test_timeout.cpp
│   └── test_capi.c                # 刻意用 C 編譯器建置
└── docs/
    ├── format.md
    ├── exec.md
    ├── capi.md
    └── architecture.md
```

**檔案大小**：沿用 aos-c 的習慣，單檔盡量 ≤ 8 KB。但**先消成因再拆檔**——
aos-c 當初兩個檔超標的真正原因是重複區塊與一個 160 行的函式，消掉重複就自然
降下來了，不必拆。硬切出來的 `_internal.h` 比一個 9 KB 的檔更難讀。

---

## 6. API 契約

### 6.1 C++（`format.hpp`）

介面吃 buffer，不吃串流：

```cpp
namespace aos {

struct ReadOptions {
    std::size_t max_record_bytes = 1u << 20;   // 1 MiB
    std::size_t max_total_bytes  = 64u << 20;  // 64 MiB
};

// 解析整份輸入。任何一筆失敗 -> 回傳失敗，out 保持清空，
// 並透過 error_line（1-based）指出是哪一行。
AOS_API InstState read_all(const char *data, std::size_t size,
                           std::vector<inst_t> &out,
                           std::size_t *error_line,
                           const ReadOptions &opts = {});

// 解析一筆。line 是不含結尾換行的一行。
AOS_API InstState read_one(const char *line, std::size_t size,
                           inst_t &out, const ReadOptions &opts = {});

// 序列化一筆，附加到 out 尾端（含結尾的 '\n'）。
// 整筆驗證通過才會寫入任何位元組。
AOS_API InstState write_one(const inst_t &inst, std::string &out);

}
```

`read_all` 是主要入口（呼應 §3.2 的原子性）。`read_one` 給 C ABI 和測試用。

### 6.2 C ABI（`aos.h`）

沿用 aos-c 的形狀，但**修掉兩件事**：

1. **不要 `FILE *`。** aos-c 的 `aos_instruction_read(FILE*)` 對其他語言的 FFI
   綁定很難用。改成 buffer 版本（`const char *`, `size_t`）與 fd 版本（`int`）。
2. 枚舉值可以重新編號（新 repo，沒有相容包袱），但**編完就凍結**，之後只能追加。
   `aos.h` 裡要寫清楚這條。

其餘規則不變：opaque handle、C++ 例外絕對不能穿過邊界（每個 `extern "C"` 函式
包 `try { ... } catch (...) { return AOS_..._ALLOC_FAILED; }`）、
`-fvisibility=hidden` + 明確的 `AOS_API` 標記。

**`aos.h` 必須能單獨用 `gcc -std=c99 -Iinclude` 編過。** `test_capi.c` 刻意用 C
編譯器建置，就是為了證明這件事——這個測試不要拿掉。

---

## 7. 里程碑

每個里程碑結束時 **build 綠、測試綠**，可以 commit。

| M | 內容 | 完成的判準 |
|---|---|---|
| **M0** | repo 骨架：CMakeLists、CMakePresets、vcpkg.json、.gitignore、Windows 的 FATAL_ERROR、一個會過的空測試 | `cmake --preset default && cmake --build --preset default && ctest --preset default` 在 WSL 上綠 |
| **M1** | `inst_t` + `format`（讀／寫／限制／未知鍵） | 格式測試全綠，含 §2.4 每一條限制 |
| **M2** | `exec`：從 aos-c 移植 fork/redirect/chdir/setenv/execvp、126/127、`128+n`。**先不做逾時** | `test_exec.cpp` 綠 |
| **M3** | 逾時 + 行程群組（§4） | `test_timeout.cpp` 綠，含「孫行程也被殺掉」這一條 |
| **M4** | `run` + `main` + CLI + docs | 端到端可用：`aos-cpp file.jsonl` 真的會跑 |
| **M5** | C ABI + shared library（soname、版本、`test_capi.c`） | `test_capi.c` 用 C 編譯器建置並通過 |
| **M6** | *（選作）* 非同步訊號安全的子行程，見 §8 | —— |

M1 和 M2 沒有相依關係，可以並行。

---

## 8. 一個要明確決定的取捨：fork 之後的非同步訊號安全

`aos-c` 的文件宣稱「子行程在 exec 之前只呼叫非同步訊號安全的函式，所以在多執行緒
程式裡也可以用」。**這句話在 aos-c 裡其實是假的**：子行程呼叫了 `setenv`（會 malloc）、
`std::string::substr`（會配置）、以及 `execvp`（POSIX 的 AS-safe 清單只有
`execl/execle/execv/execve`，帶 `p` 的不在裡面）。

實際風險：多執行緒的呼叫端，A 執行緒握著 malloc 的鎖時 B 執行緒 `fork`，
子行程只有一個執行緒、那個鎖永遠不會釋放 → 子行程一 malloc 就**死鎖**。
不是崩潰，是掛住。而這對「做成 `.so` 給別人用」是切身的——會連結共享函式庫的
程式正是最可能多執行緒的那種。

**兩條路，選一條，不要兩條都不選：**

- **(a) 修掉它（M6）**：把所有配置搬到 `fork` 之前的父行程——在父行程合併出
  `envp` 陣列、在父行程用 PATH 解析出 `argv[0]` 的完整路徑，子行程只剩
  `open`／`dup2`／`chdir`／`execve`／`_exit`，全部是 AS-safe 的。
  成本主要是自己實作 PATH 搜尋（約 40 行，注意 `PATH` 為空、含空項、
  以及 `EACCES` 要繼續找下一個而不是放棄）。
- **(b) 不修，但誠實寫在文件裡**：`docs/capi.md` 明說
  「不要從多執行緒行程呼叫 `execute`」。

**(b) 也是可以接受的答案；(a) 和 (b) 之外的「不修也不寫」不行**——
文件承諾一件實作做不到的事，比實作本身危險，因為呼叫端會照著承諾去用。

預設走 (b)，把 (a) 留在 M6。

---

## 9. 陷阱清單

每一條都是 aos-c 踩過或差點踩到的。**動手前讀一遍，M5 之後再讀一遍。**

1. **`nlohmann/json.hpp` 絕對不可以出現在 `include/` 底下任何標頭裡。**
   它一旦進了公開標頭，這個 `.so` 就變成「只給用同一版 nlohmann + 同一組編譯旗標
   的人用的函式庫」，`-fvisibility=hidden` 也救不了。只能出現在 `src/format.cpp`。
2. **測試連結不到內部符號。** `-fvisibility=hidden` 會讓沒標 `AOS_API` 的東西
   在 shared library 裡不可見。解法：用 `add_library(aos_objects OBJECT ...)`，
   測試連 object library，`aos` 連同一組 objects。不要為了測試而把內部符號 export。
3. **C++ 例外不可以穿過 `extern "C"`。** 每個 C ABI 函式都要包 `try/catch(...)`。
4. **深度限制要在解析中生效**，不是解析後檢查（那時堆疊已經爆了）。
5. **子行程用 `_exit()`，不是 `exit()`。** 後者會跑父行程登記的 `atexit`。
6. **`setpgid` 要在父子兩邊都呼叫**（§4.2），否則有競爭。
7. **`EINTR` 要重試**：`waitpid`、`read`、`open` 都要包 `do/while (… && errno == EINTR)`。
8. **指令檔的 fd 要帶 `O_CLOEXEC`。** aos-c 漏過這條，後果是子行程繼承了
   指令檔的 fd 並活過 exec；走 FIFO 時上游會永遠看到「還有讀者」而等不到 EOF。
9. **重導向 `dup2` 之後要關掉原本的 fd**（除非它剛好就是目標 fd）。
10. **macOS 沒有 `execvpe`、沒有 `pidfd_open`**。不要用。`CLOCK_MONOTONIC`、
    `setpgid`、`nanosleep` 都有，可以放心。
11. **不要用 `system()`。** 理由在 aos-c 的 `DISC_CONC_1.md`：argv 要是陣列
    （否則就是 shell injection），而且重導向／cwd／env 必須在 fork 與 exec 之間
    那個縫裡做，`system()` 沒有那個縫。
12. **`env` 的鍵要檢查**：不得為空、不得含 `=`。值可以是任何字串（JSON 會處理跳脫）。
13. **`write_one` 必須整筆驗證通過才寫第一個位元組**，否則會留下半筆記錄。
14. **錯誤訊息要帶行號**（1-based）。JSON Lines 的行號是免費的，別浪費。

---

## 10. 刻意不做的事

寫下來是為了避免「順手加一下」：

- **`_type` 鍵**——見 §0。
- **控制流（if／迴圈／並行／相依）**。這些不是欄位，是語言。
  要條件就 `argv: ["sh","-c","..."]`，要並行就 `xargs -P`，要編排就巢狀呼叫
  aos-cpp 自己。把結構搬進格式，就會失去「同時活著的行程數 = 巢狀深度」這個
  資源保證，然後你得自己管排程、fd 上限、PID 上限。
- **背景執行／不等待**。同上，它會破壞循序執行的資源上界。真的要，之後再當成
  一個欄位加（那正是換 JSON 買到的東西）。
- **`rlimit`／`umask`／`setuid`**。都是合理的欄位，但 v1 不做。
  v1 只證明「加欄位是免費的」這件事，用 `timeout_ms` 一個就夠了。
- **舊格式（九行）的讀取或轉換工具。** 舊 repo 還在，舊檔案給舊 binary 讀。
- **常駐／daemon 模式。** 沿用 aos-c：跑一次、吃到 EOF、收工。
  要「一直跑」是觸發者（cron／systemd）的事。
- **重新對齊／容錯續跑。** 任何解析失敗都停止整輪。
