# dcap

極簡的 **POSIX/UNIX**（以 Linux 為主）C / C++ 專案 scaffolder。把一個模板複製成新專案、替換 `@NAME@`、並 `git init`。只依賴 **gcc/g++ + git + make**（不使用 CMake）。

> Windows 使用者請自行在 Git Bash / MinGW 環境裡想辦法；本工具不再做任何 Windows 專屬處理。

## 唯一指令

```
dcap <template> <name>
```

- `<template>`：路徑式模板、具名外部模板、或內建的 `c` / `cpp`（永遠可用）。
- `<name>`：要建立的新專案目錄名。
- 行為：把解析到的模板目錄底下的東西全部原樣（逐位元組）複製成 `./<name>/`，只在新專案的 `Makefile`/`makefile` 內把 `@NAME@` 替換成 `<name>`（其他檔案內容與所有檔名都不變），再對新專案執行 `git init`。
- 參數不足 → 印 usage 到 stderr 並回傳 1。`<name>` 已存在 → 報錯，不覆蓋。沒有子指令、沒有 help 指令。

## 模板來源（`argv[1]` 的解析順序）

1. **路徑式**：`argv[1]` 開頭是 `.` 或 `/`（如 `./x`、`../x`、`/abs/y`）→ 視為路徑（相對呼叫 dcap 的工作目錄 cwd 或絕對路徑）。注意像 `a/b` 這種「含 `/` 但不以 `.` 或 `/` 開頭」的**不是**路徑式，會被當裸名。
2. **具名外部**：設了環境變數 `DCAP_TEMPLATES` 時，裸名會先去 `$DCAP_TEMPLATES/<名>` 找；找到就用它，因此在那裡放一個叫 `c` 或 `cpp` 的模板會**蓋掉同名內建模板**。
3. **內建**：`c`、`cpp`。以 C++20 `#embed` 編進執行檔，永遠可用，不需任何環境變數。

合法模板的判定 = 該目錄底下有 `Makefile` 或 `makefile`。找不到目錄或缺 Makefile/makefile → 報錯。

## 建置 dcap 本體

需要支援 C++20 `#embed` 的 g++（GCC 15+）與 make。

```sh
make                       # 產出 bin/dcap
```

不使用 CMake、不 static、無安裝腳本。

## 安裝

自行把本 repo 的 `bin/` 加入 PATH（dcap 執行檔本身也是這樣用）。在 `~/.bashrc` 或 `~/.zshrc` 加入（**用絕對路徑**）：

```sh
export PATH="/絕對路徑/dcap/bin:$PATH"
```

沒有 install.sh、不 copy 到系統目錄。

## 快速上手

```sh
make                       # 建置 dcap → bin/dcap，然後自行把 bin/ 加入 PATH
dcap cpp hello             # 用內建 cpp 模板建立 ./hello（並 git init）
cd hello && make run       # → 2 + 3 = 5

dcap c mytool              # 內建 C 模板
dcap ./my-template proj    # 用當前目錄下的 my-template（需含 Makefile）
DCAP_TEMPLATES=~/tpls dcap web api   # 用 ~/tpls/web 模板
```

## 產生的專案：一個檔案，兩種身分

內建模板 `make` 出來的是**單一產物** `main`，它同時是可執行檔與 shared library：

```sh
./main                                   # 直接執行
gcc yours.c -Llib -l<name> -o yours      # 當函式庫連結
dlopen("/path/to/main", RTLD_NOW)        # 或動態載入
```

作法是把它以 `-shared -fPIC` 連結成 .so，再內嵌一個 `.interp` 段指向系統的 dynamic loader（PT_INTERP，Makefile 從 `/bin/sh` 自動偵測，glibc/musl 皆可），並用 `-Wl,-e,dcap_main` 指定進入點、`-Wl,-soname` 讓它能被連結。

專案佈局：`include/` 放公開標頭、`src/` 放**全部**原始碼（沒有命名規則）、`test/` 放測試。產生的 `Makefile` 提供 `all` / `run` / `test` / `debug` / `release` / `install` / `fmt` / `clean`；`install` 把那唯一的產物裝成 `$(PREFIX)/lib/lib<name>.so`，`$(PREFIX)/bin/<name>` 是指向它的 symlink。

進入點 `dcap_main` 不是普通的 `main()`：不能 return（要自己 `exit()`）、拿不到 argc/argv、且必須掛 `__attribute__((force_align_arg_pointer))`（ELF entry 的 RSP 對齊方式與一般函式差 8 bytes，否則對齊的 SSE 存取會 segfault）。細節見 [`docs/architecture.md`](docs/architecture.md)。

## include 路徑

產生的 Makefile 的 include 就是 `-Iinclude`（不再有 `DCAP_HOME`、`cpp_libs`、`c_libs`）。C++ 模板 `g++ -std=c++20`；C 模板 `gcc -std=c11` 並連結 `-lm -lpthread`（數學 + 執行緒，基本款）。

## 文件

- 使用指南、設計說明：見 [`docs/`](docs/index.md)
- 網頁版：[`docs/html/index.html`](docs/html/index.html)
- 設計計畫（分層工作流內）：[`wf/plan/`](wf/plan/README.md)

## 授權

自用工具，未附授權條款。
