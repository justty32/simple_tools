# 設計說明

← [文件首頁](index.md)

## 定位

極簡的 **POSIX/UNIX**（以 Linux 為主）C / C++ scaffolder。已放棄跨平台——不再有任何 Windows 專屬處理、沒有 `.exe` 偵測、沒有 `-static`、沒有安裝腳本。

## 模組結構（每檔 ≤150 行）

原始碼按職責分成多個小模組：

| 檔 | 職責 |
|----|------|
| `src/main.cpp` | 進入點；解析 `argv`（`<template> <name>`）並派發到 scaffold |
| `src/scaffold.{hpp,cpp}` | 模板解析順序 → 原樣複製 + 只在 `Makefile`/`makefile` 內替換 `@NAME@` + `git init`；複製時跳過 `.git`；`print_usage()` |
| `src/builtin.{hpp,cpp}` | 內建模板：`#include` 產生出來的登錄表，查表後逐檔寫出（含建子目錄） |
| `src/paths.{hpp,cpp}` | `DCAP_TEMPLATES` 讀取與路徑正規化（`weakly_canonical`） |
| `src/util.{hpp,cpp}` | 檔案 helper、`@NAME@` 替換、`std::system` 包裝 |
| `tools/gen-embed.sh` | 掃 `templates/` 產生整份內建模板登錄表，由 Makefile 呼叫 |

**C++ 裡沒有任何地方寫死模板名稱。**`builtin.cpp` 只認得「登錄表」這個概念——`is_builtin()` 是查表、`write_builtin()` 是查表後逐檔寫出、`builtin_names()` 把表裡的名字串起來給 usage 用。要有哪些內建模板，完全由 build 時掃到什麼決定。

## 模板嵌入：`#embed`

內建範本不寫成 C++ 字串字面值，而是存成 `templates/` 下的**真實檔案**（可用一般編輯器編輯、語法 highlight）：

```
templates/
  cpp/Makefile  cpp/README.md  cpp/.clang-format  cpp/.gitignore
  cpp/include/lib.hpp  cpp/src/lib.cpp  cpp/src/main.cpp  cpp/test/test.cpp
  c/Makefile    c/README.md    c/.clang-format    c/.gitignore
  c/include/lib.h      c/src/lib.c      c/src/main.c      c/test/test.c
```

### 登錄表是產生的，不是手寫的

`#embed` 是預處理指令，檔名必須是編譯期字面值——C++ 沒有任何方式在編譯期列舉目錄。所以「自動偵測」只能發生在 build 時：`tools/gen-embed.sh` 掃 `templates/`，把整份登錄表寫成 `build/builtin_tables.inc`，`src/builtin.cpp` 再 `#include` 它。

自動偵測是**兩層**的：

1. **哪些目錄是內建模板** — `templates/` 底下每個含 `Makefile` 的目錄各成一個。判定規則跟 dcap 執行期認外部模板的規則是同一條，所以行為一致。沒有 Makefile 的目錄會被跳過並在 build 時印一行提示（而不是默默出貨一個不能用的東西）。
2. **每個模板裡有哪些檔** — 該目錄底下遞迴的所有檔案。

也就是說，**新增一個內建模板 = 建一個目錄**。不用改 C++、不用改 Makefile、不用改這份文件裡的任何清單：

```sh
mkdir -p templates/clib/src && $EDITOR templates/clib/Makefile   # 內含 @NAME@
make                                                             # dcap clib <name> 就能用了
```

Makefile 把 `templates/` 底下所有檔案列為 `.inc` 的相依，所以模板一改就重新產生、進而重編。

產生出來的內容長這樣——一個模板一個 namespace，最後一張表把它們串起來：

```cpp
namespace tpl1 {  // templates/cpp
constexpr unsigned char kE2[] = {
#embed "../templates/cpp/Makefile"
, 0};
constexpr Entry kEntries[] = {
    {"Makefile", kE2, sizeof kE2 - 1},
    // ...
};
}  // namespace tpl1

constexpr Template kTemplates[] = {
    {"c",   tpl0::kEntries, sizeof tpl0::kEntries / sizeof(Entry)},
    {"cpp", tpl1::kEntries, sizeof tpl1::kEntries / sizeof(Entry)},
};
```

幾個刻意的選擇：

- **`unsigned char` 而非 `char`**：範本檔含非 ASCII 的 UTF-8 位元組（README 是中文），存進有號 `char[]` 的 braced initializer 會 narrowing 而編譯失敗。寫出時再 `reinterpret_cast<const char*>`。
- **補一個尾端 0，但另外記真正的長度**（`sizeof - 1`）。補 0 是為了讓**空檔案**仍是合法的 initializer（`{}` 在 C++ 不合法）；記長度則讓內容含 NUL 的二進位檔也能原樣寫出，不會被截斷。空檔案由產生器直接寫成 `{0}`，連 `#embed` 都不發。
- **路徑相對於 `build/`**（`../templates/...`），因為 `.inc` 產生在那裡；`#embed "..."` 跟 `#include "..."` 一樣先找所在檔案的目錄。
- 產生器輸出**排序過**（目錄與檔案都是），所以模板沒變時重新產生的位元組完全相同，不會無謂觸發重編；檔名含 `"` 或 `\`、或目錄名含空白，都會直接報錯，而不是吐出編不過的程式碼。`templates/` 不存在或裡面一個模板都沒有也是直接失敗——空的 `kTemplates[]` 在 C++ 不合法。

寫出時依表中的相對路徑自動建立需要的子目錄，所以模板多一層 `docs/deep/note.md` 也能正常運作。唯一的限制是 `find -type f` 只列檔案，內建模板裡的**空目錄**不會保留（要保留就放個 `.gitkeep`）；外部模板走 `copy_dir()` 原樣遞迴複製，沒有這個限制。兩條路徑之後都只在新專案的 `Makefile`/`makefile` 內把 `@NAME@` 替換成專案名（其他檔案內容與所有檔名都不變）。

好處：範本與程式碼分離、可直接檢視 diff、清單不可能與實際檔案脫節。代價是多了一個 `sh` 產生步驟——所以工具鏈是 **gcc/g++ + git + make + sh**，仍然不需要 CMake 或任何 codegen 套件。內建模板永遠可用，不需任何環境變數。

## 模板解析（三種來源）

`src/scaffold.cpp` 依 `argv[1]` 決定模板，順序是**路徑式 > 具名外部 > 內建**：

| 順位 | 形式 | 判定 | 解析 |
|------|------|------|------|
| 1 | 路徑式 | 開頭是 `.` 或 `/` | 相對呼叫 dcap 的 cwd 或絕對路徑（`weakly_canonical`） |
| 2 | 具名外部 | 裸名，且 `$DCAP_TEMPLATES/<名>` 底下有 Makefile | 用該目錄；**同名時蓋過內建 c/cpp** |
| 3 | 內建 | 裸名，且在內建登錄表裡 | 直接用 `#embed` 內嵌內容，不查檔案系統 |

合法模板的判定 = 該目錄底下有 `Makefile` 或 `makefile`。找不到目錄或缺 Makefile/makefile → 報錯。`print_usage()` 也會掃 `$DCAP_TEMPLATES`，把符合這個判定的目錄列進 usage 文字。

目標目錄已存在 → 直接報錯離開，不覆蓋既有內容。

## 產生的專案：一個檔案，兩種身分

內建模板產生的**只有一個產物** `main`，它同時是可執行檔與 shared library。三個連結旗標湊出這件事：

| 旗標 | 作用 |
|------|------|
| `-shared -fPIC` | 它是一個 shared object——可被連結、可 `dlopen` |
| `.interp` 段 + `-Wl,-e,dcap_main` | 它同時可被 kernel 直接執行 |
| `-Wl,-soname,lib$(NAME).so` | 連結它的人在 runtime 找得到它 |

`PT_INTERP` 是關鍵：kernel 執行一個動態連結的 ELF 時，是把控制權交給 `.interp` 指名的 dynamic loader。平常這個段由 crt 幫可執行檔加上；shared object 沒有，所以模板在 `src/main.cpp` 裡自己宣告一個：

```cpp
extern "C" __attribute__((used, section(".interp"))) const char dcap_interp[] = DCAP_INTERP;
```

`DCAP_INTERP` 由 Makefile 於編譯期填入，從 `/bin/sh` 的 `.interp` 讀出實際的 loader 路徑（`readelf -p .interp /bin/sh | grep -o '/[^ ]*ld-[^ ]*'`），所以 glibc 與 musl 都適用，不用寫死；`make INTERP=...` 可覆蓋。

### 進入點的三個約束

`dcap_main` 是被 `-Wl,-e` 指定的 ELF entry point，不是 `main()`，所以：

1. **不能 return**——沒有 crt0 接手，必須自己 `exit()`。
2. **拿不到 argc/argv**——它們在原始堆疊上，不是函式引數。
3. **必須 `__attribute__((force_align_arg_pointer))`**——ELF entry point 進來時 RSP 是 16-byte 對齊；但一般函式的 prologue 預期的是「`call` 剛把返回位址推進去」之後的狀態，也就是 `RSP % 16 == 8`。差這 8 bytes 會讓編譯器產生的任何對齊 SSE 存取（`movaps` 之類）落在未對齊的位址上而 segfault。這是**潛伏**的：簡單的 `printf("hello")` 可能剛好沒事，`std::cout << some_int` 就炸。

第 3 點是真的踩過的坑，別把那個 attribute 拿掉。

### 為什麼不把 glibc 一起包進去

沒辦法，而且沒必要。「可執行的 .so」在定義上就需要一個 interpreter——那正是 dynamic loader；把 libc 靜態連進去就等於拿掉 interpreter，產物退回成普通執行檔，不再能被連結。另外 glibc 的 `libc.a` 不是以 PIC 編譯的，硬塞進 shared object 會在 `R_X86_64_32` 這類 relocation 上失敗；`-static-pie` 雖然免 interpreter，但產物同樣無法被 `-l` 連結。留下的相依就只有 loader 本身，任何 Linux 系統上都在。

### 其餘的 Makefile 行為

- 來源 `SRC := $(wildcard src/*.cpp)`（C 版 `src/*.c`）→ **全部**編進同一個產物，沒有命名規則。
- `lib/lib$(NAME).so` 是指向 `../main` 的 symlink，讓 `-Llib -l$(NAME)` 能用；測試就是這樣連的（配 `-Wl,-rpath,'$$ORIGIN/../lib'`）。
- targets：`all` / `run` / `test` / `debug`（clean 後 `-g -O0` 重建）/ `release` / `install` / `fmt` / `clean`。
- `install` 把那唯一的產物裝成 `$(PREFIX)/lib/lib$(NAME).so`，再讓 `$(PREFIX)/bin/$(NAME)` 成為指向它的 symlink——一份檔案，兩個入口。`PREFIX ?= /usr/local`。
- 用 `ifeq ($(origin CXX),default)` 強制 g++/gcc，但允許 `make CXX=...` 覆蓋。無 `-static`。
- include 就是 `-Iinclude`（已移除 `DCAP_HOME`）。C 模板另外連結 `-lm -lpthread`。

## 建置本體

`Makefile` 用 `g++ -std=c++20 -O2 -Wall -Wextra`，`SRC := $(wildcard src/*.cpp)`（本體原始碼都在單層 `src/`）。`TEMPLATE_FILES := $(shell find templates -type f)` 列為 `build/builtin_tables.inc` 的相依，所以改任何一個範本檔都會重新產生登錄表、進而重編。編譯時加 `-Ibuild` 讓 `builtin.cpp` 找得到產生的 `.inc`。`.DELETE_ON_ERROR:` 確保產生失敗時不會留下半截檔案給下一次 build 用。產出 `bin/dcap`；`make clean` 清掉 `bin/` 與 `build/`。不使用 CMake、不 static、無安裝腳本。
