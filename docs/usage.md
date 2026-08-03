# 使用指南

← [文件首頁](index.md)

## 安裝

需要 g++（GCC 15+，支援 C++20 與 `#embed`）與 make。

```sh
# UNIX / macOS
make
sudo ./install.sh            # 複製 bin/dcap → /usr/local/bin
#   ./install.sh ~/bin       # 或指定其他目錄

# Windows (MinGW)
mingw32-make                 # 產出 bin\dcap.exe
```

Windows 上把 `bin` 加入 PATH，或把 `dcap.exe` 複製到已在 PATH 上的目錄（例如 `C:\dev\mingw64\bin`）。

## 指令

### `dcap new <name>` — 建立 C++ 專案

在當前目錄建立 `<name>/`，內含：

- `Makefile`：`g++ -std=c++20 -O2 -Wall -Wextra`、`-I. -I$(DCAP_HOME)/cpp_libs`、`find` 遞迴搜尋所有 `.cpp`、靜態連結、輸出 `bin/<name>`。
- `main.cpp`：Hello World 範本。
- `.gitignore`（`bin/`、`build/`、`*.o`）。
- 執行 `git init`。

### `dcap new-c <name>` — 建立 C 專案

同上，但 `gcc -std=c11`、`-I$(DCAP_HOME)/c_libs`、連結 `-lm -lpthread`、搜尋 `.c`、範本為 `main.c`。

### `dcap build` — 建置並執行

1. 檢查當前目錄有 `Makefile`（沒有 → 報錯，退出碼 1）。
2. 執行 `make -j4`（Windows：`mingw32-make -j4`）。
3. 成功後於 `bin/` 找到產物並執行，印出結果與退出碼。

### `dcap help`

顯示用法。無參數等同 `help`。未知指令會報錯（退出碼 1）並印用法。

## `DCAP_HOME`：共用函式庫根目錄

產生的 Makefile 會加入 `-I$(DCAP_HOME)/cpp_libs`（C 為 `c_libs`）。`DCAP_HOME` 由環境變數指定：

| 平台 | 預設 |
|------|------|
| UNIX / macOS | `~/dev/dcap` |
| Windows | `C:/dev/dcap` |

覆蓋：`DCAP_HOME=/opt/libs dcap build` 或 `make DCAP_HOME=/opt/libs`。把你常用的共用標頭放進 `cpp_libs/` 或 `c_libs/`，任何專案就能直接 `#include`。

## 範例

```sh
dcap new hello && cd hello
dcap build
# [dcap] building with make -j4 ...
# [dcap] build ok
# [dcap] running bin/hello ...
# ----------------------------------------
# Hello, World! (C++)
# ----------------------------------------
# [dcap] program exited with code 0
```

多檔專案：把更多 `.cpp` 放進當前目錄或 `src/`，`find` 會自動納入，無需改 Makefile。

## 疑難排解

| 症狀 | 原因 / 解法 |
|------|-------------|
| `dcap.exe` 一啟動就退出、無輸出 | 若不是 `-static` 版：缺 MinGW DLL。用本專案 Makefile 重建即為靜態。 |
| Windows 上 `find: ...` 或找不到原始碼 | `mingw32-make` 用到了 `cmd.exe` 而非 Git 的 `sh`。在 Git Bash 執行，或把 `C:\Program Files\Git\usr\bin` 放到 PATH 前段。 |
| `dcap build` 找不到 `make` | 確認 `make`（UNIX）或 `mingw32-make`（Windows）在 PATH 上。 |
| 想換編譯器 | `make CXX=clang++`（C++）或 `make CC=clang`（C）；明確指定會覆蓋預設。 |
