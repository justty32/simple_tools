# 01 — 環境現況與跨平台取捨

← [plan 索引](README.md)

## 本機環境（實測）

| 依賴 | 版本 / 路徑 | 備註 |
|------|-------------|------|
| gcc / g++ | 16.1.0 @ `C:/dev/mingw64/bin` | 支援 C++20 |
| make | **只有 `mingw32-make` 4.4.1**，無 `make` | 影響 `dcap build` 與產生的 Makefile 呼叫 |
| git | 2.54.0 | `git init` 可用 |

> **不使用 CMake**（已確認）。dcap 本體用自己的 `Makefile` + g++ 建置，貼近「只用 gcc / git / make」的限制。

## 跨平台策略（已確認）

保留 UNIX 慣例字串，於**執行期**用 `#ifdef _WIN32` + `std::filesystem` 偵測，不寫死。

| 項目 | UNIX | Windows/MinGW | dcap 處理 |
|------|------|---------------|-----------|
| make 指令 | `make` | `mingw32-make` | `dcap build`：`#ifdef _WIN32` 先試 `mingw32-make`，找不到退回 `make`；反之亦然 |
| 產物副檔名 | `bin/<name>` | `bin/<name>.exe` | 產生的 Makefile 用 `EXE` 變數；`build` 找二進位時兩者都試 |
| 共用 lib 路徑 | 見下方 `DCAP_HOME` | 同 | 由環境變數指定，Makefile 內做 OS 後援 |
| 安裝路徑 | `/usr/local/bin/` | 加入 PATH | `install.sh`（UNIX）+ README 的 PATH 說明（Windows） |
| 執行子專案二進位 | `./bin/<name>` | `bin\<name>.exe` | `build` 以 `std::filesystem` 找出實際檔案再組執行字串 |

## `DCAP_HOME` 環境變數（共用 lib 根目錄）

原規範的 `$(HOME)/.cpp_libs`、`$(HOME)/.c_libs` 改為由**環境變數 `DCAP_HOME`** 指定共用 lib 根目錄：

- 預設值：UNIX `~/dev/dcap`、Windows `C:/dev/dcap`。
- C++ 專案 include：`-I. -I$(DCAP_HOME)/cpp_libs`
- C 專案 include：`-I. -I$(DCAP_HOME)/c_libs`

產生的 Makefile 內做 OS 後援，且讓環境變數可覆蓋（`?=`）：

```make
ifeq ($(OS),Windows_NT)
  DCAP_HOME ?= C:/dev/dcap
else
  DCAP_HOME ?= $(HOME)/dev/dcap
endif
```

> 目錄不存在也無害：g++/gcc 對不存在的 `-I` 路徑只會忽略。
