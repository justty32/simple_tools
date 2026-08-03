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
| `src/builtin.{hpp,cpp}` | 內建模板的派發（`is_builtin` / `write_builtin`） |
| `src/builtin_c.cpp` | 以 `#embed` 內嵌 c 模板的 8 個檔並寫出 |
| `src/builtin_cpp.cpp` | 以 `#embed` 內嵌 cpp 模板的 8 個檔並寫出 |
| `src/paths.{hpp,cpp}` | `DCAP_TEMPLATES` 讀取與路徑正規化（`weakly_canonical`） |
| `src/util.{hpp,cpp}` | 檔案 helper、`@NAME@` 替換、`std::system` 包裝 |

c / cpp 各自一個 `builtin_*.cpp`，是為了讓每檔留在 150 行內——模板檔數變多時只會往那兩檔加。

## 模板嵌入：`#embed`

內建範本不寫成 C++ 字串字面值，而是存成 `templates/` 下的**真實檔案**（可用一般編輯器編輯、語法 highlight）：

```
templates/
  cpp/Makefile  cpp/README.md  cpp/.clang-format  cpp/.gitignore
  cpp/include/lib.hpp  cpp/src/lib.cpp  cpp/src/main.cpp  cpp/test/test.cpp
  c/Makefile    c/README.md    c/.clang-format    c/.gitignore
  c/include/lib.h      c/src/lib.c      c/src/main.c      c/test/test.c
```

`src/builtin_c.cpp` / `src/builtin_cpp.cpp` 於編譯期用 C++20 的 `#embed`（GCC 15+）把每個檔案嵌入成 `constexpr unsigned char[]`：

```cpp
constexpr unsigned char kMakefile[] = {
#embed "../templates/cpp/Makefile"
    , 0};   // 尾端補 0 → C 字串
```

用 `unsigned char` 而非 `char`：範本檔含非 ASCII 的 UTF-8 位元組（README 是中文），存進有號 `char[]` 的 braced initializer 會 narrowing 而編譯失敗。寫出時再 `reinterpret_cast<const char*>`。

內建模板寫出時會先建 `include/`、`src/`、`test/` 三個目錄，再逐檔寫出；外部模板則走 `copy_dir()` 原樣遞迴複製。兩條路徑之後都只在新專案的 `Makefile`/`makefile` 內把 `@NAME@` 替換成專案名（其他檔案內容與所有檔名都不變）。好處：範本與程式碼分離、可直接檢視 diff、無需額外 codegen 工具（只靠 g++）。內建模板永遠可用，不需任何環境變數。

## 模板解析（三種來源）

`src/scaffold.cpp` 依 `argv[1]` 決定模板，順序是**路徑式 > 具名外部 > 內建**：

| 順位 | 形式 | 判定 | 解析 |
|------|------|------|------|
| 1 | 路徑式 | 開頭是 `.` 或 `/` | 相對呼叫 dcap 的 cwd 或絕對路徑（`weakly_canonical`） |
| 2 | 具名外部 | 裸名，且 `$DCAP_TEMPLATES/<名>` 底下有 Makefile | 用該目錄；**同名時蓋過內建 c/cpp** |
| 3 | 內建 | 裸名 `c` / `cpp` | 直接用 `#embed` 內嵌內容，不查檔案系統 |

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

`Makefile` 用 `g++ -std=c++20 -O2 -Wall -Wextra`，`SRC := $(wildcard src/*.cpp)`（本體原始碼都在單層 `src/`）。`TEMPLATE_FILES := $(shell find templates -type f)` 列為 `bin/dcap` 的相依，改任何一個範本檔都會觸發重編（因為它們被 `#embed` 進來）。產出 `bin/dcap`。不使用 CMake、不 static、無安裝腳本。
