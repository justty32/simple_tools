# C API

`<aos/aos.h>` 是這個專案對外的穩定介面。實作是 C++11，但跨越這道邊界的東西全部
是 C 型別、不透明指標、和一般的列舉 —— 所以你只需要一個 C 編譯器和連結器，不必
跟函式庫用同一套工具鏈。

同樣的功能也有 C++ 版本（`<aos/inst.hpp>`、`<aos/exec.hpp>`），差別不在功能而在
承諾，對照表在 [README](../README.md#該選哪一套)。簡單說：**要發行給別人、或做
跨語言綁定，用這一套。**

格式規格看 [format.md](format.md)，執行時每個欄位的行為看 [exec.md](exec.md)。
這份文件只講介面本身。

## 三個保證

**一、例外絕不會穿出來。** 沒有任何函式會拋例外、也不會因為記憶體不足就中止程式。
配置失敗會變成 `AOS_INST_ALLOC_FAILED` 或是回傳 `NULL`。這件事很重要，因為讓 C++
例外穿過 `extern "C"` 進到 C 呼叫端是未定義行為。

**二、指令是不透明的。** `aos_instruction` 你只拿得到指標，看不到裡面。所以之後
往裡面加欄位，不會讓已經編譯好的程式壞掉。

**三、列舉值只增不改。** 新狀態一律加在最後面，既有的值永遠不動。你可以安全地把
狀態值存起來或傳出去。

## 型別

### `aos_instruction`

一筆指令。不透明，只透過下面的函式操作。

### `aos_inst_state`

讀寫指令的結果。

| 值 | 數字 | 意思 |
| --- | --- | --- |
| `AOS_INST_OK` | 0 | 成功 |
| `AOS_INST_INVALID_ARGUMENT` | 1 | 傳了 `NULL`、未知的欄位、或預算為 0 |
| `AOS_INST_EOF` | 2 | 檔案乾淨結束，沒有下一筆 |
| `AOS_INST_INCOMPLETE` | 3 | 讀到一半檔案就沒了 |
| `AOS_INST_TOO_LONG` | 4 | 這筆記錄超過位元組預算 |
| `AOS_INST_READ_ERROR` | 5 | 檔案讀不動 |
| `AOS_INST_EMPTY_ARGV` | 6 | 沒有任何引數 |
| `AOS_INST_TOO_MANY_ARGS` | 7 | 引數超過上限 |
| `AOS_INST_ARGUMENT_CONTAINS_TAB` | 8 | 某個引數裡有 Tab |
| `AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK` | 9 | 某個引數裡有換行 |
| `AOS_INST_FIELD_CONTAINS_LINE_BREAK` | 10 | 某個欄位裡有換行 |
| `AOS_INST_WRITE_ERROR` | 11 | 寫不出去 |
| `AOS_INST_ENV_ENTRY_MALFORMED` | 12 | env 的某一筆不是 `KEY=VALUE` |
| `AOS_INST_TOO_MANY_ENV` | 13 | 環境變數超過上限 |
| `AOS_INST_ALLOC_FAILED` | 14 | 記憶體不足 |
| `AOS_INST_BUFFER_TOO_SMALL` | 15 | 給 `write_buffer` 的緩衝區不夠大 |

最後兩個只存在於 C 介面。C++ 那邊記憶體不足是 `std::bad_alloc`，例外不能穿過這道
邊界，所以在這裡被翻成一個狀態；緩衝區大小的問題則是因為 C++ 有
`std::ostringstream` 可以用，根本不會遇到。

### `aos_inst_field`

單一字串的欄位，順序就是它們在檔案裡的順序：

```c
AOS_FIELD_STDIN   = 0    AOS_FIELD_CWD    = 4
AOS_FIELD_STDOUT  = 1    AOS_FIELD_EXTRA  = 5
AOS_FIELD_STDERR  = 2
AOS_FIELD_EXIT    = 3
```

argv 和 env 是清單而不是字串，所以不在這裡：它們各自有 count/get/push 三個函式。

### `aos_exec_state`

執行的結果。逐項意思看 [exec.md 的狀態一覽](exec.md#狀態一覽)。

```c
AOS_EXEC_OK                    = 0    AOS_EXEC_EXIT_WRITE_FAILED    = 4
AOS_EXEC_INVALID_ARGUMENT      = 1    /* 5 已退休 */
AOS_EXEC_SPAWN_FAILED          = 2    AOS_EXEC_ALLOC_FAILED         = 6
AOS_EXEC_WAIT_FAILED           = 3
```

這裡沒有「開檔失敗」「chdir 失敗」「找不到指令」—— 那些現在都是結束碼（126 或
127），不是狀態。

5 是空的：那裡曾經是 `AOS_EXEC_PLATFORM_UNSUPPORTED`。專案改成只支援 POSIX 之後
這個狀態不可能再出現，編號留空不補，免得舊的呼叫端拿到 5 時被當成別的意思。

### `aos_exec_result`

子行程怎麼結束的。是普通的 struct，由你自己配置，函式只負責填。

```c
typedef struct aos_exec_result {
    int status;      /* 結束碼；訊號是 128+n；沒起來是 126 / 127 */
    int signalled;   /* 非零代表 status 來自訊號 */
    int error;       /* fork / wait 失敗時的 errno，否則 0 */
} aos_exec_result;
```

只有在 `aos_instruction_execute` 回傳 `AOS_EXEC_OK` 時 `status` 和 `signalled` 才
有意義。`error` 則只在 `SPAWN_FAILED`（`fork` 失敗）和 `WAIT_FAILED` 時有意義，
可以直接餵給 `strerror()`。

## 生命週期

```c
aos_instruction *aos_instruction_new(void);
void             aos_instruction_free(aos_instruction *inst);
void             aos_instruction_clear(aos_instruction *inst);
```

`new` 配置一筆空的指令，記憶體不足時回傳 `NULL` —— **要檢查**。

`free` 釋放它，傳 `NULL` 是安全的無動作。

`clear` 把它清空回剛建立的狀態，但保留內部緩衝區。**要連續讀很多筆的話，重複使用
同一個 `aos_instruction` 而不是每筆都 new/free**：緩衝區長到夠大之後就不再重新
配置了。而且 `aos_instruction_read` 本來就會先清空，所以連 `clear` 都不用自己叫。

## 讀取內容

```c
size_t      aos_instruction_argc(const aos_instruction *inst);
const char *aos_instruction_arg(const aos_instruction *inst, size_t index);
const char *aos_instruction_field(const aos_instruction *inst,
                                  aos_inst_field field);
size_t      aos_instruction_env_count(const aos_instruction *inst);
const char *aos_instruction_env(const aos_instruction *inst, size_t index);
```

`argc` 回傳引數數量；傳 `NULL` 會得到 0。

`arg` 回傳第 `index` 個引數，超出範圍或傳 `NULL` 會得到 `NULL`。

`field` 回傳單一字串欄位之一。**沒設定過的欄位是空字串 `""`，不是 `NULL`** ——
只有在傳了 `NULL` 指令或未知欄位時才會回 `NULL`。

`env_count` 和 `env` 是 argv 那一組的翻版：0 筆代表子行程繼承你的環境，有任何一筆
就是整組取代。

### 指標的存活期（重要）

`arg` 和 `field` 回傳的指標是**借來的**，由指令物件持有。任何會改動這個指令的呼叫
都會讓它失效：

- `aos_instruction_read`
- `aos_instruction_clear`
- `aos_instruction_push_arg`
- `aos_instruction_push_env`
- `aos_instruction_set_field`
- `aos_instruction_free`

要留著就自己複製一份。這是最容易踩到的陷阱 —— 特別是在讀取迴圈裡，下一次 `read`
就會讓上一筆的所有字串失效。

## 設定內容

```c
aos_inst_state aos_instruction_push_arg(aos_instruction *inst,
                                        const char *value);
aos_inst_state aos_instruction_push_env(aos_instruction *inst,
                                        const char *entry);
aos_inst_state aos_instruction_set_field(aos_instruction *inst,
                                         aos_inst_field field,
                                         const char *value);
```

`push_arg` 在 argv 尾端**追加**一個引數。第一次呼叫加進去的就是要執行的程式名稱。

`push_env` 追加一筆環境變數，收的是**一整串 `"KEY=VALUE"`**，不是 key 和 value
兩個參數 —— 那才是指令裡存的東西，也才是交給子行程的東西。會用 key/value 思考的
呼叫端手上本來就有自己的 map，攤平成字串是一次 `snprintf` 的事；做在這裡反而等於
要這個函式庫替重複的鍵訂一套合併政策，而沒有人要求過它有。

**`push_env` 不驗格式**，收下什麼就是什麼；格式是在 `write` 的時候檢查的
（`AOS_INST_ENV_ENTRY_MALFORMED`）。這樣組指令的過程不會為了一個之後才看得出來的
理由失敗。

`set_field` 設定單一字串欄位之一。

三者都會**複製**傳進去的字串，所以你之後怎麼處理它都沒關係。

回傳 `AOS_INST_OK`、`AOS_INST_INVALID_ARGUMENT`（傳了 `NULL` 或未知欄位）、或
`AOS_INST_ALLOC_FAILED`。

沒有「刪掉某個引數」的函式。要改 argv 或 env 就 `aos_instruction_clear` 之後
重建。

## 讀寫

```c
aos_inst_state aos_instruction_read(FILE *stream, aos_instruction *inst,
                                    size_t max_record_bytes);
aos_inst_state aos_instruction_write(FILE *stream,
                                     const aos_instruction *inst);
```

`read` 從串流讀下一筆。用的是 `FILE *`，所以管道、`stdin`、一般檔案都可以。

- 回傳 `AOS_INST_EOF` 代表檔案在兩筆之間乾淨結束了
- **任何失敗（含 `EOF`）都會讓 `inst` 變成空的** —— 不會有讀了一半的記錄留在裡面，
  前一筆的內容也不會殘留
- `max_record_bytes` 必須是正數，傳 `aos_inst_record_max_bytes()` 就是用預設值。
  傳 0 會被當成參數錯誤，不是「不設限」
- `INCOMPLETE` 和 `TOO_LONG` 是終點不是「等一下再試」：已經讀掉的位元組退不回串流

`write` 寫出一筆，八行每行都以 LF 結尾，所以連續呼叫就會產生一個合法的指令檔。
**整筆記錄會在寫出第一個位元組之前全部驗證完**，所以一筆不合法的指令不會弄髒你的
輸出檔。但如果是寫到一半 I/O 出錯，那還是可能留下半筆。

### Append 到檔案末端

沒有專門的 append 函式，也不需要 —— `write` 只往前寫、從不 seek，所以用 append
模式開檔就是了：

```c
FILE *f = fopen("insts", "a");     /* "a" 而不是 "w" */

aos_instruction_write(f, first);
aos_instruction_write(f, second);   /* 接在後面，記錄之間不需要分隔符號 */
fclose(f);
```

同一個串流連續呼叫也是接續往後寫，所以「累積一個指令檔」就是開著串流一直寫。

用 `"a"` 開檔還有一個額外好處：底層是 `O_APPEND`，每次寫入都會定位到當時的檔案
結尾，所以多個行程同時往同一個指令檔追加也不會互相蓋掉。

之所以不提供 `aos_instruction_append(const char *path, ...)` 這種便利函式，是因為
整個函式庫刻意不碰檔案路徑 —— 開檔的策略（模式、權限、要不要建立）留給你決定，
函式庫只處理已經開好的串流。

## 序列化成 bytes

```c
aos_inst_state aos_instruction_write_buffer(const aos_instruction *inst,
                                            char *buffer, size_t size,
                                            size_t *needed);
```

想要把一筆指令變成記憶體裡的一段位元組（塞進封包、交給別的函式庫、或用非 `FILE *`
的方式寫出去）就用這個。內容跟 `aos_instruction_write` 寫出來的完全一樣。

用的是 C 慣見的兩段式：**先問大小，再給緩衝區。**

```c
size_t needed = 0;
char *buf;

/* 第一次：只問大小。回傳 AOS_INST_BUFFER_TOO_SMALL 是正常的 */
aos_instruction_write_buffer(inst, NULL, 0, &needed);

buf = malloc(needed + 1);           /* +1 是給結尾的 NUL */
if (buf == NULL) { /* ... */ }

if (aos_instruction_write_buffer(inst, buf, needed + 1, &needed)
    == AOS_INST_OK) {
    /* buf 現在是這筆指令的位元組，而且有 NUL 結尾 */
}
free(buf);
```

規則：

- `*needed` 是序列化後的長度，**不含**結尾的 NUL
- 緩衝區必須放得下那個 NUL，所以 `size` 要**嚴格大於** `*needed`。剛好相等會得到
  `AOS_INST_BUFFER_TOO_SMALL`
- 緩衝區太小的時候 `*needed` **還是會被填好**，這正是第一次呼叫拿到大小的方式
- 驗證失敗（引數含 Tab、沒有引數之類）會回傳對應的狀態，而且 `*needed` 留在 0
- `needed` 可以傳 `NULL`，如果你已經知道大小
- 結果有 NUL 結尾只是為了方便；記錄本身不可能含有 NUL，所以直接用長度也一樣安全

## 執行

```c
aos_exec_state aos_instruction_execute(aos_instruction *inst,
                                       aos_exec_result *result);
```

把這筆指令跑起來，等它結束。

`result` 可以傳 `NULL`，如果你只在意回傳的狀態。

**子行程回傳非零不算失敗**，那會是 `AOS_EXEC_OK` 加上 `result.status` 帶著結束碼。
**「指令根本沒起來」也不算** —— 那是 `AOS_EXEC_OK` 加上 127（找不到指令）或 126
（重導向、`chdir` 失敗），跟 shell 一樣。真正的失敗只剩 `fork`、等待、寫 exit 檔
這三種。這個分界線和背後的理由寫在 [exec.md](exec.md#什麼算失敗)。

這個函式只有 POSIX 實作，而整個函式庫也只在 POSIX 上建置得起來，所以沒有「這個
平台不支援」這種回傳值。

## 查詢

```c
size_t      aos_inst_argv_max(void);
size_t      aos_inst_env_max(void);
size_t      aos_inst_record_max_bytes(void);
const char *aos_inst_state_string(aos_inst_state state);
const char *aos_exec_state_string(aos_exec_state state);
const char *aos_version_string(void);
```

前三個回傳**這個函式庫編譯時**的上限值。要用就呼叫它們，不要自己寫死常數 —— 動態
連結的時候，實際生效的是函式庫裡的值，不是你手上那份標頭裡的。

兩個 `_state_string` 回傳靜態的英文說明字串，**永遠不會是 `NULL`**，就算你傳一個
範圍外的值也一樣（會得到 `"unknown ..."`）。不用釋放。

`aos_version_string` 回傳 `"主版本.次版本.修訂"`。標頭裡另外有
`AOS_VERSION_MAJOR` / `_MINOR` / `_PATCH` 三個巨集，可以拿來做編譯期檢查 ——
比對這兩者就能發現「編譯時的標頭」和「執行時的函式庫」版本不一致。

## 完整範例

### 讀一個檔案並執行

```c
#include <aos/aos.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    aos_instruction *inst;
    FILE *f;
    aos_inst_state state;
    size_t index = 0;

    if (argc != 2) {
        fprintf(stderr, "用法：%s 指令檔\n", argv[0]);
        return 2;
    }
    if ((f = fopen(argv[1], "r")) == NULL) {
        perror(argv[1]);
        return 1;
    }
    if ((inst = aos_instruction_new()) == NULL) {
        fprintf(stderr, "記憶體不足\n");
        fclose(f);
        return 1;
    }

    /* 同一個 inst 重複使用，緩衝區長夠了就不再配置 */
    while ((state = aos_instruction_read(f, inst,
                                         aos_inst_record_max_bytes()))
           == AOS_INST_OK) {
        aos_exec_result result;
        aos_exec_state exec_state = aos_instruction_execute(inst, &result);

        /* 一筆跑不起來就記下來繼續，後面的指令跟它沒有關係 */
        if (exec_state != AOS_EXEC_OK) {
            fprintf(stderr, "第 %zu 筆跑不起來：%s（%s）\n", index,
                    aos_exec_state_string(exec_state), strerror(result.error));
        } else {
            /* 注意：這個指標下一次 read 之後就失效了 */
            printf("第 %zu 筆 %s 結束，狀態 %d\n", index,
                   aos_instruction_arg(inst, 0), result.status);
        }
        ++index;
    }

    if (state != AOS_INST_EOF) {
        fprintf(stderr, "第 %zu 筆讀不了：%s\n", index,
                aos_inst_state_string(state));
    }

    aos_instruction_free(inst);
    fclose(f);
    return 0;
}
```

> **`fopen` 開的 fd 會被每個子行程繼承。** `fork` 複製所有 fd，而 fd 預設會活過
> `exec`。普通檔案上這只是不好看；來源是 FIFO 或管道時是掛死風險 —— 上游會一直
> 看到「還有讀者」而等不到 EOF。要避免的話：`fopen(path, "re")` 的 `e` 就是
> `O_CLOEXEC`，但它是 glibc／musl／BSD 的擴充而不是標準 C；要可攜就自己
> `open(path, O_RDONLY | O_CLOEXEC)` 再 `fdopen()`。

### 產生一個指令檔

```c
#include <aos/aos.h>
#include <stdio.h>

int main(void)
{
    aos_instruction *inst = aos_instruction_new();
    FILE *f = fopen("insts", "w");

    if (inst == NULL || f == NULL) {
        return 1;
    }

    aos_instruction_push_arg(inst, "echo");
    aos_instruction_push_arg(inst, "hello world");   /* 空白是普通字元 */
    aos_instruction_set_field(inst, AOS_FIELD_STDOUT, "out.txt");
    aos_instruction_set_field(inst, AOS_FIELD_EXIT, "status.txt");

    if (aos_instruction_write(f, inst) != AOS_INST_OK) {
        fprintf(stderr, "寫不出去\n");
    }

    /* 第二筆：清空後重建，不要另外 new 一個 */
    aos_instruction_clear(inst);
    aos_instruction_push_arg(inst, "date");
    aos_instruction_write(f, inst);

    aos_instruction_free(inst);
    fclose(f);
    return 0;
}
```

## 連結

```sh
gcc -std=c99 my.c -laos -o my                    # 動態連結
gcc -std=c99 my.c -I/路徑/include -L/路徑/lib -laos -o my
```

在本專案裡建出來的話：

```sh
make shared                                       # 產生 build/debug/libaos.so
gcc -std=c99 my.c -Iinclude -Lbuild/debug -laos \
    -Wl,-rpath,$PWD/build/debug -o my
```

連結器要用 C 編譯器就好 —— 函式庫自己已經帶著它需要的 C++ 執行期相依，你不需要
知道那件事。

共享函式庫的 soname 是 `libaos.so.0`，只有在 C ABI 破壞時才會進位。C++ 介面沒有
做任何承諾，所以它不會影響 soname。

## 執行緒

函式庫本身沒有全域狀態。

- **不同的 `aos_instruction` 在不同執行緒同時用，是安全的。**
- **同一個 `aos_instruction` 在多個執行緒同時用，不安全** —— 自己加鎖。
- `aos_instruction_execute` 會 `fork`。子行程在 `exec` 之前只呼叫非同步訊號安全的
  函式，所以在多執行緒程式裡也可以用。
- 三個 `_string` 函式回傳的是靜態字串，任何時候呼叫都安全。

## ABI 穩定性：什麼會變，什麼不會

**不會變的：**

- 既有列舉值的數字
- 既有函式的簽章
- `aos_exec_result` 已經有的欄位的意義與位置
- `aos_instruction` 的不透明性

**可能會變的：**

- 新的列舉值會加在**最後面**。所以 `switch` 記得寫 `default`，或至少能容忍不認得
  的值
- 新的函式會加進來
- `aos_instruction` 裡面的東西隨時會變 —— 這正是它不透明的原因

**會進 soname 的（也就是破壞相容性的）：**

- 移除或改變既有函式
- 重排或插入列舉值
- 改動 `aos_exec_result` 的佈局

目前是 `0.1.0`。版本號 0 開頭意思是介面還可能大改；真的要當成穩定介面依賴之前，
先確認版本已經到 1.x。
