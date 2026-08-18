# aos-c

讀一個指令檔，把裡面每一筆當成一個行程跑起來。

實作是 C++11，沒有任何外部相依，用一份 Makefile 建置。對外同時提供 **C** 和 **C++**
兩套介面。

## 快速開始

指令檔裡一筆指令佔八行。用 `printf` 產生一筆（`\t` 是 Tab，八行都要有換行）：

```sh
printf 'echo\thello world\n\nout.txt\n\nstatus.txt\n\n\n\n' > insts
```

跑它：

```sh
aos-c insts
cat out.txt      # hello world
cat status.txt   # 0
```

八行分別是 argv、stdin、stdout、stderr、exit、cwd、env、extra。留空代表照預設來。
env 那一行放的是環境變數本身（Tab 分隔的 `KEY=VALUE`），不是一個檔案路徑。
完整規格看 [docs/format.md](docs/format.md)，執行時每個欄位的行為看
[docs/exec.md](docs/exec.md)。

> **注意**：執行一個指令檔等於用你的權限跑任意指令。對指令檔的寫入權限，等同於
> 任意程式碼執行權限。
>
> 而且記錄之間沒有分隔符號，所以**任何一筆多一行或少一行，後面全部錯位**，並且會
> 產生語法合法、內容卻來自別筆的指令 —— 解析器偵測不到，執行起來就是跑一個你沒
> 寫過的指令。**請用 `aos_instruction_write` 產生指令檔，不要手寫。**
> 詳見 [docs/exec.md](docs/exec.md#-錯位是偵測不到的而且會執行你沒寫過的指令)。

## 編譯

需要 GCC（或任何 C++11 編譯器）和 GNU Make，沒有其他相依。

```sh
make debug          # 建 build/debug/aos-c，有除錯符號
make release        # 建 build/release/aos-c，-O2
make test           # 建測試並執行
make strict         # 加上 -Werror 全部重來一次，然後跑測試
make shared         # 建共享函式庫 build/<mode>/libaos.so
make clean          # 清掉整個 build/
```

### Windows

用 MinGW-w64 的話把 `make` 換成 `mingw32-make.exe`。整個專案（含測試）可以編譯、
連結、執行，`make shared` 會產出 `aos.dll` 加一個匯入函式庫。

**但行程建立目前只有 POSIX 實作。** 讀取、寫入、序列化在 Windows 上都正常，
`aos_instruction_execute` 則直接回傳 `AOS_EXEC_PLATFORM_UNSUPPORTED` —— 也就是說
`aos-c` 這個程式在 Windows 上跑不了任何指令。測試套件會跳過執行相關的案例
（POSIX 上 150 個，Windows 上 118 個）。

靜態連結時記得定義 `AOS_STATIC`，見 [docs/capi.md](docs/capi.md)。

`make strict` 是送出改動前該跑的那一個：它用 `-Wall -Wextra -Wpedantic -Werror`
重建全部，然後跑完整測試套件。

## 當程式用

```sh
aos-c insts        # 讀一個檔案
aos-c fifo         # 或一條 named FIFO —— 讀到寫入端關閉為止
aos-c < insts      # 或從標準輸入讀，管線也可以
producer | aos-c   # 邊產生邊執行
```

指令依序執行，而且**會一路跑到底**：

- 程式回傳非零 —— 不影響後面。那是資料，會寫進 exit 欄位指定的檔案
- 某筆根本跑不起來（找不到指令、cwd 不存在、重導向開不了）—— 一樣不影響後面，
  它是一筆結束碼 127 或 126 的記錄，跟 shell 一樣
- 記錄格式壞掉 —— **停止整條串流**。記錄之間沒有分隔符號，錯位之後無法重新對齊，
  硬讀下去會把後面每一筆都解成別筆欄位拼出來的垃圾

全部成功時完全不輸出，回傳 0。有任何一筆失敗就回傳 1，並在最後印出
`aos: N of M instructions failed`。細節見 [docs/exec.md](docs/exec.md#多筆指令)，
串流用法見 [docs/exec.md](docs/exec.md#串流餵指令)。

## 當函式庫用

兩套介面，選一套。差別**不在功能**，在承諾。

### C 介面：`<aos/aos.h>`

這是穩定的發行邊界。跨越它的全部是 C 型別、不透明指標、和一般的列舉，所以只需要
一個 C 編譯器和連結器，不必跟函式庫用同一套工具鏈。例外絕不會穿過這道邊界，記憶體
不足會回報成狀態而不是拋出。

```c
#include <aos/aos.h>
#include <stdio.h>

int main(void)
{
    aos_instruction *inst = aos_instruction_new();
    FILE *f = fopen("insts", "r");
    aos_inst_state state;

    while ((state = aos_instruction_read(f, inst,
                                         aos_inst_record_max_bytes()))
           == AOS_INST_OK) {
        aos_exec_result result;

        if (aos_instruction_execute(inst, &result) == AOS_EXEC_OK) {
            printf("%s -> %d\n", aos_instruction_arg(inst, 0), result.status);
        }
    }

    aos_instruction_free(inst);
    fclose(f);
    return 0;
}
```

```sh
gcc -std=c99 my.c -laos -o my
```

### C++ 介面：`<aos/inst.hpp>`、`<aos/exec.hpp>`

同樣的功能，但直接用 `std::string`、`std::vector` 和 `std::istream`，不必再包一層。

```cpp
#include <aos/exec.hpp>
#include <aos/inst.hpp>
#include <fstream>
#include <iostream>

int main()
{
    std::ifstream in("insts");
    aos::inst_t inst;

    while (aos::read_instruction(in, inst) == aos::InstState::Ok) {
        aos::ExecResult result;

        if (aos::execute(inst, result) == aos::ExecState::Ok) {
            std::cout << inst.argv[0] << " -> " << result.status << '\n';
        }
    }
    return 0;
}
```

```sh
g++ -std=c++11 my.cpp -laos -o my
```

### 該選哪一套

| | C（`aos.h`） | C++（`inst.hpp` / `exec.hpp`） |
| --- | --- | --- |
| 需要什麼 | 一個 C 編譯器 | 跟函式庫**完全相同**的編譯器、標準函式庫、ABI 設定 |
| 跨版本相容 | 有 —— 列舉值和函式簽章只增不改 | 沒有承諾 |
| 記憶體不足 | 回傳 `AOS_INST_ALLOC_FAILED` | 拋出 `std::bad_alloc` |
| 手動釋放 | 要（`aos_instruction_free`） | 不用 |

簡單的判準：**自己的專案、跟函式庫一起建置 → 用 C++ 那套比較舒服。要發行給別人、
或跨語言綁定 → 用 C 那套。**

C++ 介面之所以沒有跨版本承諾，是因為介面上出現 `std::string` 和 `std::vector` 就
等於要求呼叫端跟函式庫用同一套標準函式庫實作和同一個 `_GLIBCXX_USE_CXX11_ABI`
設定。這不是缺陷，是這種介面的本質；C 那一套的存在就是為了給需要真正穩定邊界的人
用。

## 目錄結構

```text
include/aos/aos.h        公開 C API（穩定邊界）
include/aos/inst.hpp     公開 C++ API：指令型別、讀取、寫入
include/aos/exec.hpp     公開 C++ API：執行一筆指令
include/aos/export.h     符號可見度巨集，上面三個共用
src/                     實作、C shim、以及 aos-c 這個程式本身
tests/                   測試，無框架，只用 CHECK 巨集
docs/                    見下
```

`src/` 裡的東西都不是公開介面。所有內部符號都在匿名命名空間裡，加上
`-fvisibility=hidden`，所以不會意外出現在共享函式庫的表面。

## 文件

要**寫指令檔**：

- [docs/format.md](docs/format.md) —— 指令檔格式：八行分別是什麼、Tab 怎麼分隔、
  哪些字元有特殊意義、大小限制。
- [docs/exec.md](docs/exec.md) —— 執行行為：每個欄位在跑的時候做什麼、什麼算失敗、
  多筆指令的順序與中止規則、安全性。

要**寫程式呼叫它**：

- [docs/capi.md](docs/capi.md) —— C API 逐項說明：型別、每個函式、指標存活期、
  序列化成 bytes、ABI 穩定性規則。
- [docs/cxxapi.md](docs/cxxapi.md) —— C++ API 逐項說明，以及跟 C 那套的對照。

要**改這份程式碼**：

- [docs/architecture.md](docs/architecture.md) —— 設計決策與取捨（英文）。分層為什麼
  切在這裡、放棄了什麼、shim 到底在做什麼、ABI 上還有什麼沒解決。
