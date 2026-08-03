# 01 — 環境與設計取捨

← [plan 索引](README.md)

## 定位取捨：POSIX-only（已確認）

dcap 定位為極簡的 **POSIX/UNIX**（以 Linux 為主）scaffolder。**已放棄跨平台**：

- 不再有 `#ifdef _WIN32`、`.exe` / `mingw32-make` 偵測、`-static`、安裝腳本。
- Windows 使用者請自行在 Git Bash / MinGW 環境裡想辦法（不提供教學）。

> **不使用 CMake**（已確認）。dcap 本體用自己的 `Makefile` + g++ 建置，貼近「只用 gcc / git / make」的限制。

## 依賴

| 依賴 | 用途 | 備註 |
|------|------|------|
| g++ | 建置 dcap 本體 | 需支援 C++20 `#embed`（GCC 15+）|
| make | 建置 dcap 本體與產生的專案 | 產生的 Makefile 亦用 `find`、`mkdir -p` |
| git | 對新專案 `git init` | scaffold 最後一步 |

## include 路徑（已移除 `DCAP_HOME`）

**產生的 Makefile** 的 include 現在就是 `INCLUDES := -I.`——已移除 `DCAP_HOME`，不再有 `-I$(DCAP_HOME)/cpp_libs`、`-I$(DCAP_HOME)/c_libs`。

- C++ 模板：`g++ -std=c++20 -I.`
- C 模板：`gcc -std=c11 -I. ... -lm -lpthread`（數學 + 執行緒，基本款，保留）

## `DCAP_TEMPLATES`（具名外部模板根目錄）

用於**模板解析**：設了此環境變數時，c/cpp 以外的裸名會去 `$DCAP_TEMPLATES/<名>` 找。沒設 → 只有內建 c/cpp 可用。內建 c/cpp 以 `#embed` 編進執行檔，**不需任何環境變數**，永遠可用。
