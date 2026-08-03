# dcap 文件

dcap 是極簡的 **POSIX/UNIX**（以 Linux 為主）C / C++ 專案 scaffolder：把模板複製成新專案、替換 `@NAME@`、並 `git init`。

## 目錄

| 文件 | 內容 |
|------|------|
| [usage.md](usage.md) | 建置與安裝、唯一指令、模板三種來源、範例、疑難排解 |
| [architecture.md](architecture.md) | 模組結構、`#embed` 模板嵌入、模板解析、產生的 Makefile |

網頁版（可用瀏覽器直接開啟）：

| HTML | |
|------|--|
| [html/index.html](html/index.html) | 總覽 |
| [html/usage.html](html/usage.html) | 使用指南 |
| [html/architecture.html](html/architecture.html) | 設計說明 |

## 一分鐘版

```sh
make                       # 建置 dcap → bin/dcap，然後自行把 bin/ 加入 PATH
dcap cpp hello             # 用內建 cpp 模板建立 ./hello（並 git init）
cd hello && make run       # → Hello, World! (C++)
```

- `dcap <template> <name>`：把模板原樣複製成 `./<name>/`、只在 `Makefile`/`makefile` 內替換 `@NAME@`、`git init`。
- `<template>` = `c`（`gcc -std=c11 -lm -lpthread`）、`cpp`（`g++ -std=c++20`）、具名外部（`$DCAP_TEMPLATES/<名>`）或路徑式（開頭是 `.` 或 `/`）。
- 產生出來的專案用 `make` / `make run` / `make clean` 自行建置與執行。

## 設計原則

- **POSIX-only**：已放棄跨平台，不再有任何 Windows 專屬處理。
- **只依賴 gcc/g++ + git + make**，不使用 CMake。
- **內建模板即真檔**：範本存於 `templates/`，以 C++20 `#embed` 於編譯期嵌入執行檔，永遠可用。
- **單一指令**：只有 `dcap <template> <name>`，沒有子指令。
- **單檔 ≤150 行**：原始碼按職責分成多個小模組。
