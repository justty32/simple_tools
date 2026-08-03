# dcap

輕量、跨平台（Windows/MinGW + UNIX）的 CLI 自動化建置工具，用來隨手快速建立與管理純 **C** 或 **C++** 的腳本式小專案。只依賴 **gcc/g++ + git + make**（不使用 CMake）。

```
dcap new <name>     建立 C++ 小專案（g++ -std=c++20）
dcap new-c <name>   建立純 C 小專案（gcc -std=c11 -lm -lpthread）
dcap build          建置當前專案並執行產出的二進位
dcap help           顯示說明
```

## 建置 dcap 本體

需要 g++（支援 C++20 與 `#embed`，GCC 15+）與 make。

```sh
# UNIX / macOS
make
sudo ./install.sh          # 或 sudo make install → /usr/local/bin/dcap

# Windows (MinGW)
mingw32-make               # 產出 bin\dcap.exe
# 再把 bin 加入 PATH，或複製 dcap.exe 到 C:\dev\mingw64\bin
```

`dcap.exe` 以 `-static` 靜態連結，可獨立執行（不需 MinGW 的 DLL 在 PATH 上）。

## 快速上手

```sh
dcap new hello
cd hello
dcap build          # 編譯並印出 Hello, World! (C++)
```

`dcap new` 會建立 `Makefile`、`main.cpp`（或 `main.c`）、`.gitignore`，並執行 `git init`。

## 共用函式庫路徑：`DCAP_HOME`

產生的 Makefile 會把共用標頭目錄加入 include 搜尋路徑：

- C++ 專案：`-I. -I$(DCAP_HOME)/cpp_libs`
- C 專案：`-I. -I$(DCAP_HOME)/c_libs`

`DCAP_HOME` 由環境變數指定，預設值：

| 平台 | 預設 `DCAP_HOME` |
|------|------------------|
| UNIX / macOS | `~/dev/dcap` |
| Windows | `C:/dev/dcap` |

覆蓋範例：`DCAP_HOME=/opt/mylibs dcap build`（或在 make 時 `make DCAP_HOME=...`）。目錄不存在也無妨，編譯器會忽略。

## Windows 注意事項（重要）

產生的 Makefile 用 `$(shell find . -name '*.cpp')` 遞迴搜尋原始碼、並用 `mkdir -p`。這些需要 **Git 附帶的 `sh` / `find`**（`C:\Program Files\Git\usr\bin`）。`mingw32-make` 只要在 PATH 上找得到該 `sh.exe` 就會自動採用，一切正常運作。

建議：**在 Git Bash 內執行**，或把 Git 的 `usr\bin` 放到 PATH 前段。否則 `mingw32-make` 會退回 `cmd.exe`，`find` 會誤用 Windows 內建的 `find.exe`（語意不同）而失敗。

## 文件

- 使用指南、設計說明：見 [`docs/`](docs/index.md)
- 網頁版：[`docs/html/index.html`](docs/html/index.html)
- 設計計畫（分層工作流內）：[`wf/plan/`](wf/plan/README.md)

## 授權

自用工具，未附授權條款。
