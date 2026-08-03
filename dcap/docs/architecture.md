# 設計說明

← [文件首頁](index.md)

## 定位

極簡的 **POSIX/UNIX**（以 Linux 為主）C / C++ scaffolder。已放棄跨平台——不再有任何 Windows 專屬處理、沒有 `.exe` 偵測、沒有 `-static`、沒有安裝腳本。

## 模組結構（每檔 ≤150 行）

原始碼按職責分成多個小模組：

| 檔 | 職責 |
|----|------|
| `src/main.cpp` | 進入點；解析 `argv`（`<template> <name>`）並派發到 scaffold |
| `src/scaffold.{hpp,cpp}` | 解析模板 → 原樣複製 + 只在 `Makefile`/`makefile` 內替換 `@NAME@` + `git init`；複製時跳過 `.git`；`print_usage()` |
| `src/builtin.{hpp,cpp}` | 以 `#embed` 內嵌 c / cpp 兩個內建模板 |
| `src/paths.{hpp,cpp}` | `DCAP_TEMPLATES` 與路徑解析（具名外部 / 路徑式） |
| `src/util.{hpp,cpp}` | 檔案 helper、`@NAME@` 替換、`std::system` 包裝 |

## 模板嵌入：`#embed`

內建範本不寫成 C++ 字串字面值，而是存成 `templates/` 下的**真實檔案**（可用一般編輯器編輯、語法 highlight）：

```
templates/
  cpp/Makefile   cpp/main.cpp   cpp/.gitignore
  c/Makefile     c/main.c
```

`src/builtin.cpp` 於編譯期用 C++20 的 `#embed`（GCC 15+）把每個檔案嵌入成 `constexpr char[]`：

```cpp
constexpr char kCppMakefile[] = {
#embed "../templates/cpp/Makefile"
    , 0};   // 尾端補 0 → C 字串
```

複製時模板底下的東西全部原樣複製，只在新專案的 `Makefile`/`makefile` 內把 `@NAME@` 佔位符替換成專案名（其他檔案內容與所有檔名都不變）。好處：範本與程式碼分離、可直接檢視 diff、無需額外 codegen 工具（只靠 g++）。內建模板永遠可用，不需任何環境變數。

## 模板解析（三種來源）

`src/paths.cpp` 依 `argv[1]` 決定模板目錄：

| 形式 | 判定 | 解析 |
|------|------|------|
| 內建 | `c` / `cpp` | 直接用 `#embed` 內嵌內容，不查檔案系統 |
| 路徑式 | 開頭是 `.` 或 `/` | 相對呼叫 dcap 的 cwd 或絕對路徑（`weakly_canonical`） |
| 具名外部 | 裸名（不以 `.` 或 `/` 開頭）且非 c/cpp | `$DCAP_TEMPLATES/<名>`；未設環境變數則無此來源 |

合法模板的判定 = 該目錄底下有 `Makefile` 或 `makefile`。找不到目錄或缺 Makefile/makefile → 報錯。

## 產生的 Makefile

產生給子專案的 Makefile 用 `$(shell find . -name '*.cpp')`（C 版 `*.c`）做無限遞迴，新增檔案不用改 Makefile。輸出 `bin/<name>`（無 `.exe`），含 `all` / `run` / `clean`。用 `ifeq ($(origin CXX),default)` 強制 g++/gcc，但允許 `make CXX=...` 覆蓋。無 `-static`。

include 就是 `INCLUDES := -I.`（已移除 `DCAP_HOME`）。C 模板仍連結 `-lm -lpthread`。

## 建置本體

`Makefile` 用 `g++ -std=c++20`，`SRC := $(wildcard src/*.cpp)`（本體原始碼都在單層 `src/`）。`templates/*` 列為相依，改範本會觸發重編（因為它們被 `#embed` 進來）。產出 `bin/dcap`。不使用 CMake、不 static、無安裝腳本。
