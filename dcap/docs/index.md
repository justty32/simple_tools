# dcap 文件

dcap 是極簡的 **POSIX/UNIX**（以 Linux 為主）C / C++ 專案 scaffolder：把模板複製成新專案、替換 `@NAME@`、並 `git init`。

## 目錄

| 文件 | 內容 |
|------|------|
| [usage.md](usage.md) | 建置與安裝、唯一指令、模板三種來源、產生的專案與其 make targets、疑難排解 |
| [architecture.md](architecture.md) | 模組結構、`#embed` 模板嵌入、模板解析順序、「可執行的 .so」怎麼做到的 |

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
cd hello && make run       # → 2 + 3 = 5
```

- `dcap <template> <name>`：把模板原樣複製成 `./<name>/`、只在 `Makefile`/`makefile` 內替換 `@NAME@`、`git init`。
- `<template>` = 路徑式（開頭是 `.` 或 `/`）、`$DCAP_TEMPLATES/<名>`、或內建（目前 `c` / `cpp`）——依此順序解析。
- 內建模板產生的是**單一產物**：一個 `main`，既能 `./main` 直接執行，也能被 `-l<name>` 連結或 `dlopen`。佈局為 `include/` `src/` `test/`。
- 產生出來的專案用 `make` / `make run` / `make test` / `make install` 等自行建置與執行。

## 設計原則

- **POSIX-only**：已放棄跨平台，不再有任何 Windows 專屬處理。
- **只依賴 gcc/g++ + git + make + sh**，不使用 CMake（`make fmt` 另需 clang-format）。
- **內建模板即真檔**：範本存於 `templates/`，以 C++20 `#embed` 於編譯期嵌入執行檔，永遠可用。
- **內建清單非寫死**：`templates/` 底下每個含 `Makefile` 的目錄自動成為一個內建模板，**新增一個 = 建一個目錄**，C++ 完全不用動。
- **單一指令**：只有 `dcap <template> <name>`，沒有子指令。
- **單檔 ≤150 行**：原始碼按職責分成多個小模組。
