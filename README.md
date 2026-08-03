# dcap

極簡的 **POSIX/UNIX**（以 Linux 為主）C / C++ 專案 scaffolder。把一個模板複製成新專案、替換 `@NAME@`、並 `git init`。只依賴 **gcc/g++ + git + make**（不使用 CMake）。

> Windows 使用者請自行在 Git Bash / MinGW 環境裡想辦法；本工具不再做任何 Windows 專屬處理。

## 唯一指令

```
dcap <template> <name>
```

- `<template>`：`c` 或 `cpp`（內建，永遠可用），或具名外部模板，或路徑式模板。
- `<name>`：要建立的新專案目錄名。
- 行為：把解析到的模板目錄底下的東西全部原樣（逐位元組）複製成 `./<name>/`，只在新專案的 `Makefile`/`makefile` 內把 `@NAME@` 替換成 `<name>`（其他檔案內容與所有檔名都不變），再對新專案執行 `git init`。
- 參數不足 → 印 usage 到 stderr 並回傳 1。沒有子指令、沒有 help 指令。

## 模板來源（`argv[1]` 的三種解析）

- **內建**：`c`、`cpp`。以 C++20 `#embed` 編進執行檔，永遠可用，不需任何環境變數。
- **具名外部**：設了環境變數 `DCAP_TEMPLATES` 時，c/cpp 以外的裸名會去 `$DCAP_TEMPLATES/<名>` 找；沒設就只有 c/cpp 可用。
- **路徑式**：`argv[1]` 開頭是 `.` 或 `/`（如 `./x`、`../x`、`/abs/y`）→ 視為路徑（相對呼叫 dcap 的工作目錄 cwd 或絕對路徑）。注意像 `a/b` 這種「含 `/` 但不以 `.` 或 `/` 開頭」的**不是**路徑式，會被當裸名。

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
cd hello && make run       # → Hello, World! (C++)

dcap c mytool              # 內建 C 模板
dcap ./my-template proj    # 用當前目錄下的 my-template（需含 Makefile）
DCAP_TEMPLATES=~/tpls dcap web api   # 用 ~/tpls/web 模板
```

## 產生的專案內容

`Makefile`、`main.cpp`（或 `main.c`）、`.gitignore`（`bin/`、`build/`、`*.o`），並已 `git init`。

產生的 Makefile（POSIX）：

- `SRC := $(shell find . -name '*.cpp')`（C 版為 `*.c`）遞迴搜尋原始碼。
- 輸出 `bin/<name>`（無 `.exe`）；含 `all` / `run`（`./bin/<name>`）/ `clean` targets。
- 用 `ifeq ($(origin CXX),default)` 強制 g++/gcc，但允許 `make CXX=...` 覆蓋。無 `-static`。

## 建置 / 執行「產生出來的專案」

直接用 `make` / `make run` / `make clean`（dcap 不再包裝建置）。

## include 路徑

產生的 Makefile 的 include 就是 `INCLUDES := -I.`（不再有 `DCAP_HOME`、`cpp_libs`、`c_libs`）。C++ 模板 `g++ -std=c++20 -I.`；C 模板 `gcc -std=c11 -I. ... -lm -lpthread`（數學 + 執行緒，基本款）。

## 文件

- 使用指南、設計說明：見 [`docs/`](docs/index.md)
- 網頁版：[`docs/html/index.html`](docs/html/index.html)
- 設計計畫（分層工作流內）：[`wf/plan/`](wf/plan/README.md)

## 授權

自用工具，未附授權條款。
