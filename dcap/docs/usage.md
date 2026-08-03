# 使用指南

← [文件首頁](index.md)

## 建置 dcap 本體

需要 g++（GCC 15+，支援 C++20 與 `#embed`）與 make。

```sh
make                          # 產出 bin/dcap
```

不使用 CMake、不 static、無安裝腳本。

## 安裝

自行把本 repo 的 `bin/` 加入 PATH（dcap 執行檔本身也是這樣用）。在 `~/.bashrc` 或 `~/.zshrc` 加入下面這行（**用絕對路徑**，因為 rc 檔載入時 `$PWD` 不會是本目錄）：

```sh
export PATH="/絕對路徑/dcap/bin:$PATH"
```

沒有 install.sh、不 copy 到系統目錄。

> Windows 使用者請自行在 Git Bash / MinGW 環境裡想辦法。

## 唯一指令：`dcap <template> <name>`

- `<template>`：要用的模板（三種來源見下）。
- `<name>`：要建立的新專案目錄名。

行為：把解析到的模板目錄底下的東西全部原樣（逐位元組）複製成 `./<name>/`，複製時跳過 `.git`，只在新專案的 `Makefile`/`makefile` 內把 `@NAME@` 替換成 `<name>`（其他檔案內容與所有檔名都不變），最後對新專案執行 `git init`。

參數不足 → 印 usage 到 stderr 並回傳 1。沒有子指令、沒有 help 指令。`<name>` 已存在 → 報錯（退出碼 1），不覆蓋。

usage 除了固定文字外，還會把 `$DCAP_TEMPLATES` 底下找得到的具名模板一併列出來。

## 模板的三種來源

解析順序：**路徑式（`.` / `/` 開頭）> `$DCAP_TEMPLATES/<名>` > 內建**。

### 內建（目前是 `c` / `cpp`）

以 C++20 `#embed` 編進執行檔，永遠可用，不需任何環境變數。內建有哪些**不是寫死的**——`templates/` 底下每個含 `Makefile` 的目錄都會在 build 時自動成為一個內建模板。跑 `dcap` 不帶參數就會列出這份執行檔實際帶了哪些。

- `dcap cpp <name>` → C++：`g++ -std=c++20 -O2 -Wall -Wextra -Iinclude`。
- `dcap c <name>` → C：`gcc -std=c11 -O2 -Wall -Wextra -Iinclude`，另連結 `-lm -lpthread`。

兩者產生的都是**單一產物**專案——一個既可執行、又可被連結的 `main`（見下節），並 `git init`。

#### 自己加一個內建模板

建一個目錄就好，不用改任何程式碼：

```sh
mkdir -p templates/clib/src templates/clib/include
$EDITOR templates/clib/Makefile      # 裡面用 @NAME@ 當專案名佔位符
make                                 # 重建 dcap
dcap clib mylib                      # 就能用了
```

規則跟外部模板一樣：**該目錄底下要有 `Makefile` 或 `makefile`**，否則 build 時會印一行提示並跳過它。目錄名就是模板名（不能含空白），檔名不能含 `"` 或 `\`。目錄底下可以有任意層數的子目錄，會照原樣重建。

用 `.gitkeep` 之類的佔位檔保留空目錄——內建模板只帶檔案，空目錄不會保留（外部模板走的是原樣複製，沒有這個限制）。

### 具名外部：`$DCAP_TEMPLATES/<名>`

若設了環境變數 `DCAP_TEMPLATES`，裸名會**先**去 `$DCAP_TEMPLATES/<名>` 找；找到（且該目錄含 Makefile）就用它。因此在那裡放一個與內建同名的模板會**蓋掉那個內建模板**。沒設 `DCAP_TEMPLATES` → 只有內建的可用。

```sh
DCAP_TEMPLATES=~/tpls dcap web api   # 用 ~/tpls/web 模板建立 ./api
```

### 路徑式

`<template>` 開頭是 `.` 或 `/`（如 `./x`、`../x`、`/abs/y`）→ 視為路徑（相對呼叫 dcap 的工作目錄 cwd 或絕對路徑）。注意像 `a/b` 這種「含 `/` 但不以 `.` 或 `/` 開頭」的**不是**路徑式，會被當裸名。

```sh
dcap ./my-template proj              # 用當前目錄下的 my-template
```

> 合法模板的判定 = 該目錄底下有 `Makefile` 或 `makefile`。找不到目錄或缺 Makefile/makefile → 報錯（退出碼 1）。

## 產生出來的專案：一個檔案，兩種身分

內建 c / cpp 模板 `make` 出來的是**單一產物** `main`，它同時是可執行檔與 shared library：

```sh
./main                                   # 直接執行
gcc yours.c -Llib -l<name> -o yours      # 當函式庫連結
dlopen("/path/to/main", RTLD_NOW)        # 或動態載入
```

作法是把它以 `-shared -fPIC` 連結成 .so，再內嵌一個 `.interp` 段指向系統的 dynamic loader（PT_INTERP），並用 `-Wl,-e,dcap_main` 指定進入點；`-Wl,-soname` 讓它能被連結。`lib/lib<name>.so` 只是指向 `../main` 的 symlink，好讓 `-Llib -l<name>` 找得到。

loader 路徑由 Makefile 從 `/bin/sh` 的 `.interp` 自動偵測（glibc、musl 都適用）；偵測失敗時自己指定：

```sh
make INTERP=/lib64/ld-linux-x86-64.so.2
```

專案內容：

```
<name>/
├── Makefile           # 唯一被替換 @NAME@ 的檔
├── README.md
├── .clang-format
├── .gitignore         # main、test/test、lib/、build/、*.o
├── include/lib.hpp    # C 版為 lib.h — 公開標頭
├── src/lib.cpp        # C 版為 lib.c — 實作
├── src/main.cpp       # C 版為 main.c — 進入點 dcap_main
└── test/test.cpp      # C 版為 test.c
```

- `include/` — 公開標頭，他人以 `-Iinclude` 引用。
- `src/*.cpp`（C 版 `src/*.c`）— **全部**編進同一個產物，沒有命名規則；新增檔案直接放進 `src/` 即可。
- `test/` — 測試，連結產物本身。

## 寫進入點要注意

`dcap_main` 不是普通的 `main()`：

- **不能 return** — 沒有 crt0 接手，必須自己呼叫 `exit()`。
- **拿不到 argc/argv** — 需要的話讀 `/proc/self/cmdline`。
- **必須掛 `__attribute__((force_align_arg_pointer))`** — ELF entry point 進來時 RSP 是 16-byte 對齊，但函式 prologue 預期的是「call 之後」的 `RSP % 16 == 8`；差這 8 bytes 會讓任何對齊的 SSE spill 直接 segfault（例如 `std::cout << some_int` 或 `printf("%f", x)`）。模板已經掛好了，改寫進入點時別拿掉。

其他函式照常寫，完全不受影響。

## 建置 / 執行產生的專案

dcap 不包裝建置，直接用產生的 Makefile：

```sh
cd <name>
make                                 # 建 main（+ lib/lib<name>.so symlink）
make run                             # 執行 ./main
make test                            # 建置並執行 test/test
make debug                           # clean 後以 -g -O0 重建
make release                         # 同 all
make install                         # 裝到 PREFIX（預設 /usr/local）
make fmt                             # clang-format -i 格式化
make clean                           # 移除 lib/、main、test/test、*.o
```

`make install` 把那唯一的產物裝成 `$(PREFIX)/lib/lib<name>.so`，再讓 `$(PREFIX)/bin/<name>` 成為指向它的 symlink——一份檔案，兩個入口。換前綴：`make install PREFIX=~/.local`。

LSP 用的 `compile_commands.json` 可用 `bear -- make` 產生。

## include 路徑

產生的 Makefile 的 include 就是 `-Iinclude`（已移除 `DCAP_HOME`，不再有 `cpp_libs`、`c_libs`）。C 模板另外連結 `-lm -lpthread`（數學 + 執行緒，基本款）。

## 範例

```sh
dcap cpp hello && cd hello
make run
# 2 + 3 = 5
make test
# all tests passed
file main
# main: ELF 64-bit LSB shared object, ..., interpreter /lib64/ld-linux-x86-64.so.2
```

## 疑難排解

| 症狀 | 原因 / 解法 |
|------|-------------|
| `dcap` 找不到 | 尚未把本 repo 的 `bin/` 加入 PATH；用絕對路徑加到 `~/.bashrc` / `~/.zshrc`。 |
| `error: template not found` | 裸名不在內建清單裡、`$DCAP_TEMPLATES` 底下也沒有，或路徑打錯。跑 `dcap` 不帶參數看實際有哪些內建。 |
| 新加的 `templates/<名>` 沒出現 | 該目錄缺 `Makefile`／`makefile`（build 時會印 `gen-embed: skipping ...`），或忘了重新 `make`。 |
| `error: not a template (no Makefile)` | 該目錄底下沒有 `Makefile` 或 `makefile`；模板必須含其一。 |
| `error: '<name>' already exists` | 目標目錄已存在；dcap 不覆蓋，換個名字或先移除。 |
| 用 `c`/`cpp` 卻長得不像內建模板 | `$DCAP_TEMPLATES` 底下有同名模板，會蓋掉內建；unset 該變數或改名。 |
| `cannot detect the dynamic loader` | 從 `/bin/sh` 讀不到 `.interp`（缺 readelf 或環境特殊）；改用 `make INTERP=/lib64/ld-linux-x86-64.so.2`。 |
| `./main` 執行時 `required file not found` | 內嵌的 loader 路徑不對；用 `readelf -p .interp main` 對照 `readelf -p .interp /bin/sh`，再用 `make INTERP=...` 重建。 |
| 改了進入點之後 `./main` segfault | 進入點少了 `force_align_arg_pointer`，或 return 了而沒呼叫 `exit()`。 |
| 建置產生的專案找不到 `make` | 確認 `make` 在 PATH 上。 |
| `make fmt` 失敗 | 需要 `clang-format` 在 PATH 上（模板附了 `.clang-format`）。 |
| 想換編譯器 | `make CXX=clang++`（C++）或 `make CC=clang`（C）；明確指定會覆蓋預設。 |
