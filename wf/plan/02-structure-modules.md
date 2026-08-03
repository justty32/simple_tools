# 02 — 本體結構與模組拆分

← [plan 索引](README.md)

## dcap 本體專案結構

```
dcap/
├── AGENTS.md / CLAUDE.md / wf/   # 工作流（非侵入式導入）
├── Makefile                      # 建置 dcap 本體 (g++ -std=c++20)
├── src/                          # dcap 原始碼（多模組，每檔 ≤150 行）
├── templates/                    # #embed 用的內建範本真檔（cpp/、c/）
├── README.md                     # 用法（POSIX/UNIX）
├── docs/                         # 說明文件（含 docs/html/）
└── .gitignore
```

## 模組拆分（單檔 ≤150 行，已確認）

| 檔 | 職責 |
|----|------|
| `src/main.cpp` | 進入點；解析 `argv`（`<template> <name>`）並派發到 scaffold |
| `src/scaffold.hpp` / `.cpp` | 解析模板 → 複製 + 替換 `@NAME@` + `git init`；複製時跳過 `.git`；`print_usage()` |
| `src/builtin.hpp` / `.cpp` | 以 `#embed` 內嵌 c / cpp 內建模板 |
| `src/paths.hpp` / `.cpp` | `DCAP_TEMPLATES` 與路徑解析（具名外部 / 路徑式） |
| `src/util.hpp` / `.cpp` | 檔案 helper（讀寫、`ensure_dir`）、`@NAME@` 替換、`std::system` 包裝 |

> 若任一 `.cpp` 逼近 150 行，再依職責續拆。

## dcap 本體 Makefile（示意）

```make
ifeq ($(origin CXX),default)
  CXX := g++
endif
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
SRC      := $(wildcard src/*.cpp)
BIN      := bin/dcap
all: $(BIN)
# templates/ 被 src/builtin.cpp #embed 進來，列為 build inputs
$(BIN): $(SRC) $(wildcard templates/*/* templates/*/.gitignore)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC) -o $@
clean:
	rm -rf bin
.PHONY: all clean
```

> 本體 Makefile 用 `wildcard src/*.cpp`（來源都在單層 `src/`），輸出 `bin/dcap`（無 `.exe`）。`templates/*` 列為相依，因為它們被 `#embed` 進來。**無 `-static`、無 `install` target**：安裝＝自行加 PATH（見 [04](04-deploy-verify-risks.md)）。**產生給子專案**的 Makefile 則用 `find` 無限遞迴（見 [03](03-commands.md)）。
