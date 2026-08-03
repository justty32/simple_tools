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

參數不足 → 印 usage 到 stderr 並回傳 1。沒有子指令、沒有 help 指令。

## 模板的三種來源

### 內建：`c` / `cpp`

以 C++20 `#embed` 編進執行檔，永遠可用，不需任何環境變數。

- `dcap cpp <name>` → C++：`g++ -std=c++20 -I.`、`find` 遞迴搜尋 `.cpp`、範本 `main.cpp`。
- `dcap c <name>` → C：`gcc -std=c11 -I.`、連結 `-lm -lpthread`、搜尋 `.c`、範本 `main.c`。

兩者都產生 `.gitignore`（`bin/`、`build/`、`*.o`）並 `git init`。

### 具名外部：`$DCAP_TEMPLATES/<名>`

若設了環境變數 `DCAP_TEMPLATES`，則 c/cpp 以外的裸名會去 `$DCAP_TEMPLATES/<名>` 找。沒設 → 只有 c/cpp 可用。

```sh
DCAP_TEMPLATES=~/tpls dcap web api   # 用 ~/tpls/web 模板建立 ./api
```

### 路徑式

`<template>` 開頭是 `.` 或 `/`（如 `./x`、`../x`、`/abs/y`）→ 視為路徑（相對呼叫 dcap 的工作目錄 cwd 或絕對路徑）。注意像 `a/b` 這種「含 `/` 但不以 `.` 或 `/` 開頭」的**不是**路徑式，會被當裸名。

```sh
dcap ./my-template proj              # 用當前目錄下的 my-template
```

> 合法模板的判定 = 該目錄底下有 `Makefile` 或 `makefile`。找不到目錄或缺 Makefile/makefile → 報錯（退出碼 1）。

## 建置 / 執行產生的專案

dcap 不再包裝建置，直接用產生的 Makefile：

```sh
cd <name>
make                                 # 編譯 → bin/<name>
make run                             # 執行 ./bin/<name>
make clean                           # 移除 bin/
```

多檔專案：把更多 `.cpp`（或 `.c`）放進專案目錄任何層級，`find` 會自動納入，無需改 Makefile。

## include 路徑

產生的 Makefile 的 include 就是 `INCLUDES := -I.`（已移除 `DCAP_HOME`，不再有 `cpp_libs`、`c_libs`）。C 模板仍連結 `-lm -lpthread`（數學 + 執行緒，基本款）。

## 範例

```sh
dcap cpp hello && cd hello
make run
# ----------------------------------------
# Hello, World! (C++)
# ----------------------------------------
```

## 疑難排解

| 症狀 | 原因 / 解法 |
|------|-------------|
| `dcap` 找不到 | 尚未把本 repo 的 `bin/` 加入 PATH；用絕對路徑加到 `~/.bashrc` / `~/.zshrc`。 |
| `error: template not found` | 裸名非 c/cpp 且未設 `DCAP_TEMPLATES`，或路徑打錯。 |
| `error: not a template (no Makefile)` | 該目錄底下沒有 `Makefile` 或 `makefile`；模板必須含其一。 |
| 建置產生的專案找不到 `make` | 確認 `make` 在 PATH 上。 |
| 想換編譯器 | `make CXX=clang++`（C++）或 `make CC=clang`（C）；明確指定會覆蓋預設。 |
