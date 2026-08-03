# 02 — 本體結構與模組拆分

← [plan 索引](README.md)

## dcap 本體專案結構

```
dcap/
├── AGENTS.md / CLAUDE.md / wf/   # 工作流（非侵入式導入）
├── Makefile                      # 建置 dcap 本體 (g++ -std=c++20)
├── src/                          # dcap 原始碼（多模組，每檔 ≤150 行）
├── install.sh                    # UNIX 安裝腳本
├── README.md                     # 用法（Windows + UNIX）
├── docs/                         # 說明文件（含 docs/html/）
└── .gitignore
```

## 模組拆分（單檔 ≤150 行，已確認）

原規範的「單檔 main.cpp」改為多模組，每檔 ≤150 行：

| 檔 | 職責 |
|----|------|
| `src/main.cpp` | 進入點；解析 `argv[1]` 並派發到各指令 |
| `src/platform.hpp` / `.cpp` | 平台工具：`make_program()`、`exe_suffix()`、`dcap_home()`、`run(cmd)`（`std::system` 包裝）|
| `src/util.hpp` / `.cpp` | 檔案工具：`write_file()`、`ensure_dir()`、`exists()` |
| `src/templates.hpp` | 模板字串產生器宣告 |
| `src/templates_cpp.cpp` | C++ 專案模板：Makefile、`main.cpp`、`.gitignore` |
| `src/templates_c.cpp` | C 專案模板：Makefile、`main.c`、`.gitignore` |
| `src/commands.hpp` / `.cpp` | `cmd_new()`、`cmd_build()`、`print_usage()` |

> 若任一 `.cpp` 逼近 150 行，再依職責續拆（templates 已先按語言切兩檔）。

## dcap 本體 Makefile（示意）

```make
CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
SRC      := $(wildcard src/*.cpp)
EXE      := $(if $(filter Windows_NT,$(OS)),.exe,)
BIN      := bin/dcap$(EXE)
all: $(BIN)
$(BIN): $(SRC) | bin
	$(CXX) $(CXXFLAGS) $(SRC) -o $@
bin:
	mkdir -p bin
install: all
	cp $(BIN) /usr/local/bin/
clean:
	rm -rf bin
```

> 本體 Makefile 用 `wildcard src/*.cpp`（來源都在 `src/`，單層即可）。**產生給子專案**的 Makefile 則用 `find` 無限遞迴（見 [03](03-commands.md)）。
