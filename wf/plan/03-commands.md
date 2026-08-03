# 03 — CLI 指令規格

← [plan 索引](README.md)

## `dcap new <name>` — 建立 C++ 小專案

在當前目錄建立 `<name>/`，內含：
- `Makefile`：`g++ -std=c++20`、`-I. -I$(DCAP_HOME)/cpp_libs`、`find` 遞迴搜尋所有 `.cpp`、輸出 `bin/<name>(.exe)`。
- `main.cpp`：Hello World 範本。
- `git init` + `.gitignore`（含 `bin/`、`build/`、`*.o`）。

## `dcap new-c <name>` — 建立純 C 小專案

在當前目錄建立 `<name>/`，內含：
- `Makefile`：`gcc -std=c11`、`-I. -I$(DCAP_HOME)/c_libs`、連結 `-lm -lpthread`、`find` 遞迴搜尋所有 `.c`、輸出 `bin/<name>(.exe)`。
- `main.c`：Hello World 範本。
- `git init` + `.gitignore`。

## `dcap build` — 建置並執行當前專案

1. 檢查當前目錄有 `Makefile`，無則報錯退出（非零碼）。
2. 呼叫 `make -j4`（平台化：Windows `mingw32-make -j4`／UNIX `make -j4`；找不到再退回另一個）。
3. 成功 → 於 `bin/` 找對應二進位（含 `.exe`）並執行，印出結果；失敗 → 印錯誤訊息與非零退出碼。

## 其他

- `dcap`（無參數）或 `dcap help` → 印用法。
- 未知指令 → 印錯誤 + 用法，退出碼 1。
- `dcap build` 目前**不**傳額外參數給子專案二進位（可再議）。

## 產生的子專案 Makefile — 檔案搜尋

採用 `find` 無限遞迴（已確認）：

```make
SRC := $(shell find . -name '*.cpp')   # C 版為 '*.c'
```

Windows 上依賴 Git 附帶的 `find.exe`（見 [04 風險 R5](04-deploy-verify-risks.md)）。
