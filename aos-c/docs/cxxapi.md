# C++ API

`<aos/inst.hpp>` 和 `<aos/exec.hpp>` 是 C++ 這一套介面。功能跟 [C API](capi.md)
完全一樣，差別在於這裡直接用 `std::string`、`std::vector` 和 `std::istream`，不必
再隔一層。

格式規格看 [format.md](format.md)，執行時每個欄位的行為看 [exec.md](exec.md)。

## 先講清楚：它不保證跨版本相容

介面上出現 `std::string` 和 `std::vector`，就代表呼叫端必須跟函式庫用**完全相同
的**編譯器、標準函式庫實作、和 `_GLIBCXX_USE_CXX11_ABI` 設定。不一致的話不會得到
連結錯誤，而是未定義行為。

所以判準很簡單：

- **自己的專案，跟函式庫一起編譯** → 用這一套，比較舒服
- **要發行給別人、或做跨語言綁定** → 用 [C API](capi.md)

這不是缺陷，是這種介面的本質。C 那一套的存在就是為了給需要真正穩定邊界的人用。

## 型別

### `aos::inst_t`

一筆指令。是個普通的 struct，欄位全部公開，沒有私有區塊，沒有隱藏狀態：

```cpp
struct inst_t {
    std::vector<std::string> argv;
    std::string stdin_path;
    std::string stdout_path;
    std::string stderr_path;
    std::string exit_path;
    std::string cwd;
    std::vector<std::string> env;   /* 每個元素是一整串 "KEY=VALUE" */
    std::string extra;

    void clear();
    bool empty() const;   /* argv 是空的就是 true */
};
```

**每個欄位都擁有自己的位元組。** 所以一筆指令在它來源的串流關掉之後仍然有效，可以
複製、可以放進容器、可以搬移，不需要記得釋放任何東西。手工組一筆出來寫檔，跟讀進來
的那筆是同一個型別，沒有兩套所有權規則。

`clear()` 清空所有欄位但保留各個字串的容量，所以用同一個 `inst_t` 連續讀整條
串流時，緩衝區會被重複使用。`read_instruction` 自己會先呼叫它，你通常不用管。

### `aos::InstState`

讀寫的結果，`enum class`：

```cpp
Ok  InvalidArgument  Eof  Incomplete  TooLong  ReadError
EmptyArgv  TooManyArgs
ArgumentContainsTab  ArgumentContainsLineBreak  FieldContainsLineBreak
WriteError  EnvEntryMalformed  TooManyEnv  MissingSeparator
```

意思跟 [C API 的對照表](capi.md#aos_inst_state)一樣，但這裡**少兩個**：C 那邊的
`ALLOC_FAILED` 和 `BUFFER_TOO_SMALL` 在這裡不存在 —— 記憶體不足是 `std::bad_alloc`
例外，而緩衝區大小的問題交給 `std::ostringstream` 處理掉了。`MissingSeparator`
兩邊都有，代表八行欄位之後那一行不是空的（記錄錯位）；它的數值是 16，跳過那兩個
C 專屬狀態佔住的 14、15。

### `aos::ExecState` 與 `aos::ExecResult`

```cpp
enum class ExecState {
    Ok, InvalidArgument,
    SpawnFailed,        /* 只剩 fork 失敗 */
    WaitFailed, ExitWriteFailed
};

struct ExecResult {
    int status = 0;        /* 結束碼；訊號是 128+n；沒起來是 126 / 127 */
    bool signalled = false;
    int error = 0;         /* fork / wait 失敗時的 errno */
};
```

### 常數

```cpp
constexpr std::size_t kInstArgvMax        = 256;        /* 引數數量上限 */
constexpr std::size_t kInstEnvMax         = 256;        /* 環境變數數量上限 */
constexpr std::size_t kInstLineCount      = 8;          /* 欄位行數；記錄含分隔共九行 */
constexpr std::size_t kInstRecordMaxBytes = 1024 * 1024; /* 預設位元組預算 */
```

動態連結的時候，**實際生效的是函式庫裡編譯進去的值**，不是你手上這份標頭裡的。要
確保一致就呼叫 `aos::inst_argv_max()` 與 `aos::inst_env_max()`。

## 函式

```cpp
InstState read_instruction(std::istream &in, inst_t &inst,
                           std::size_t max_record_bytes = kInstRecordMaxBytes);

InstState write_instruction(std::ostream &out, const inst_t &inst);

ExecState execute(inst_t &inst, ExecResult &result);

std::size_t inst_argv_max();
std::size_t inst_env_max();

const char *to_string(InstState state);
const char *to_string(ExecState state);

std::vector<char *> to_c_argv(inst_t &inst);
```

### `read_instruction`

從串流讀下一筆。因為吃的是 `std::istream`，所以檔案、`std::cin`、
`std::istringstream` 都可以。

- 回傳 `InstState::Eof` 代表串流在兩筆之間乾淨結束
- **任何失敗（含 `Eof`）都會讓 `inst` 變成空的**，不會有讀了一半的記錄殘留
- `max_record_bytes` 必須是正數。它擋的是「一行無限長把記憶體吃光」，指令檔是信任
  邊界，所以預算永遠存在，傳 0 會被當成參數錯誤而不是「不設限」
- `Incomplete` 和 `TooLong` 是終點，不是「等一下再試」—— 已讀掉的位元組退不回串流

### `write_instruction`

寫出一筆，九行（八行欄位加一行空白分隔）每行都以 LF 結尾。**整筆會在寫出第一個
位元組之前全部驗證完**，所以不合法的指令不會弄髒輸出。寫到一半 I/O 出錯還是可能
留下半筆，這個無法避免。

### `execute`

把指令跑起來，等它結束。取的是 `inst_t` 而不是路徑或串流，所以測試時不用碰
檔案系統。

**子行程回傳非零不算失敗**，**指令根本沒起來也不算** —— 兩者都是 `ExecState::Ok`
加上 `result.status`（沒起來就是 126 或 127，跟 shell 一樣）。分界線和背後的理由
在 [exec.md](exec.md#什麼算失敗)。

參數是非 const 參考，但**它不會改動你的指令** —— 只是因為底下要呼叫 `to_c_argv`。

### `to_c_argv`

把 `inst_t` 轉成 `execv` 那一家要的 `char *const *`，最後補一個 `nullptr`。

```cpp
std::vector<char *> argv = aos::to_c_argv(inst);
execvp(argv[0], argv.data());
```

指標**借用** `inst.argv` 裡字串的儲存空間，所以只在 `inst` 還活著而且沒被改動的
期間有效。

這個函式存在的唯一理由是 `execv` 要的是 `char *const *` 而不是
`const char *const *`。C++ 消不掉這個歷史包袱，能做的只是把它關在一個函式裡，而不
是讓每個呼叫端各自 `const_cast`。

env 沒有對應的 `to_c_envp`：`execute` 是在子行程裡逐筆 `setenv` 來擴充繼承的環境，
不需要先攤成 `char **`，所以那個 helper 已經移除。

## 常見用法

### 讀一個檔案，全部跑完

```cpp
#include <aos/exec.hpp>
#include <aos/inst.hpp>
#include <fstream>
#include <iostream>

std::ifstream in("insts");
aos::inst_t inst;          /* 同一個重複使用，緩衝區長夠了就不再配置 */
aos::InstState state;

while ((state = aos::read_instruction(in, inst)) == aos::InstState::Ok) {
    aos::ExecResult result;
    aos::ExecState exec_state = aos::execute(inst, result);

    /* 這裡的失敗只剩 fork/wait/寫檔；記下來繼續，後面的指令跟它沒關係 */
    if (exec_state != aos::ExecState::Ok) {
        std::cerr << "跑不起來：" << aos::to_string(exec_state) << '\n';
        continue;
    }
    std::cout << inst.argv[0] << " -> " << result.status << '\n';
}
if (state != aos::InstState::Eof) {
    std::cerr << "讀不了：" << aos::to_string(state) << '\n';
}
```

> **這個例子的來源是 `std::ifstream`，所以它開的 fd 會被每個子行程繼承。**
> `fork` 會複製所有 fd，而 fd 預設會活過 `exec`。普通檔案上這只是不好看；
> 來源是 FIFO 或管道時是掛死風險 —— 上游會一直看到「還有讀者」而等不到 EOF。
> 要避免就別用 `std::ifstream`：自己 `open(path, O_RDONLY | O_CLOEXEC)` 再包成
> 串流，這正是 `aos-c` 這個程式在 `run.cpp` 裡做的事。

### 把一筆指令變成 bytes

C++ 這邊不需要專門的函式，`std::ostringstream` 就是：

```cpp
#include <sstream>

std::ostringstream out;

if (aos::write_instruction(out, inst) == aos::InstState::Ok) {
    std::string bytes = out.str();      /* 這就是可以寫進檔案的那段位元組 */
}
```

（C API 沒有 `ostringstream` 可用，所以那邊有一個專門的
[`aos_instruction_write_buffer`](capi.md#序列化成-bytes)。）

反過來，從記憶體裡的 bytes 讀回一筆：

```cpp
std::istringstream in(bytes);
aos::inst_t inst;

aos::read_instruction(in, inst);
```

### Append 到檔案末端

同樣不需要專門的函式 —— 用 append 模式開檔就好，`write_instruction` 本來就只往前
寫、從不 seek：

```cpp
std::ofstream out("insts", std::ios::app);

aos::write_instruction(out, first);
aos::write_instruction(out, second);   /* 接在後面，分隔的空白行由 write 自己補 */
```

同一個串流連續呼叫也是接續往後寫，所以「累積一個指令檔」就是開著串流一直寫。

### 手工組一筆出來寫檔

```cpp
aos::inst_t inst;

inst.argv = { "echo", "hello world" };   /* 空白是普通字元，這是兩個引數 */
inst.stdout_path = "out.txt";
inst.exit_path = "status.txt";

std::ofstream out("insts");
aos::write_instruction(out, inst);
```

其他欄位維持預設的空字串就好，那代表「照預設來」。

## 例外

實作本身不會為了回報錯誤而拋例外 —— 輸入用完、記錄格式壞掉、串流失敗，全部是
`InstState` 回傳值。

**唯一會拋的是記憶體不足**：`std::string` 和 `std::vector` 會丟 `std::bad_alloc`
（極端情況也可能是 `std::length_error`）。這是刻意的，因為在 C++ 裡把配置失敗做成
回傳值，等於要求每一次字串操作都檢查。

也不會對串流呼叫 `exceptions()`，所以你自己設的串流例外遮罩會維持原樣。

## 執行緒

- 不同的 `inst_t` 在不同執行緒同時用，安全
- 同一個 `inst_t` 在多執行緒同時用，不安全，自己加鎖
- 函式庫沒有全域狀態
- `execute` 會 `fork`。子行程在 `exec` 之前只呼叫非同步訊號安全的函式，所以在多
  執行緒程式裡也可以用
- `to_string` 回傳靜態字串，隨時呼叫都安全

## 跟 C API 的對照

| | C++ | C |
| --- | --- | --- |
| 型別 | `aos::inst_t`（欄位公開） | `aos_instruction *`（不透明） |
| 讀取 | `read_instruction(istream&, ...)` | `aos_instruction_read(FILE *, ...)` |
| 寫入 | `write_instruction(ostream&, ...)` | `aos_instruction_write(FILE *, ...)` |
| 變成 bytes | `std::ostringstream` | `aos_instruction_write_buffer` |
| 從 bytes 讀 | `std::istringstream` | `aos_instruction_read_buffer` |
| 執行 | `execute(inst_t&, ExecResult&)` | `aos_instruction_execute` |
| 取用引數 | `inst.argv[i]` | `aos_instruction_arg(inst, i)` |
| 設定欄位 | `inst.cwd = "/tmp"` | `aos_instruction_set_field(...)` |
| 釋放 | 解構子 | `aos_instruction_free` |
| 記憶體不足 | `std::bad_alloc` | `AOS_INST_ALLOC_FAILED` |
| 跨版本相容 | 沒有承諾 | 有 |

## 連結

```sh
g++ -std=c++11 my.cpp -laos -o my
g++ -std=c++11 my.cpp -Iinclude -Lbuild/debug -laos \
    -Wl,-rpath,$PWD/build/debug -o my
```

函式庫用 `-fvisibility=hidden` 建置，所以只有標了 `AOS_API` 的東西看得到 —— 共有
8 個 C++ 進入點，`read_line`、`split_argv` 這些內部函式一個都不在符號表上。

Windows 上靜態連結時要定義 `AOS_STATIC`，理由和用法跟
[C API 那邊](capi.md#windows靜態連結要定義-aos_static)一樣。
