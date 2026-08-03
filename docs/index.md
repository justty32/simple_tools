# dcap 文件

dcap 是輕量、跨平台的 CLI 工具，用來快速建立與建置純 C / C++ 的腳本式小專案。

## 目錄

| 文件 | 內容 |
|------|------|
| [usage.md](usage.md) | 安裝、三個指令、`DCAP_HOME`、範例、疑難排解 |
| [architecture.md](architecture.md) | 模組結構、`#embed` 模板嵌入、跨平台策略、靜態連結 |

網頁版（可用瀏覽器直接開啟）：

| HTML | |
|------|--|
| [html/index.html](html/index.html) | 總覽 |
| [html/usage.html](html/usage.html) | 使用指南 |
| [html/architecture.html](html/architecture.html) | 設計說明 |

## 一分鐘版

```sh
make                 # 建置 dcap（Windows 用 mingw32-make）
dcap new hello       # 建立 C++ 小專案 + git init
cd hello
dcap build           # 編譯並執行 → Hello, World! (C++)
```

- `dcap new <name>` → C++（`g++ -std=c++20`）
- `dcap new-c <name>` → C（`gcc -std=c11 -lm -lpthread`）
- `dcap build` → `make -j4`（Windows：`mingw32-make -j4`）後執行 `bin/` 內的產物

## 設計原則

- **只依賴 gcc/g++ + git + make**，不使用 CMake。
- **跨平台**：保留 UNIX 慣例字串，執行期以 `#ifdef _WIN32` + `std::filesystem` 偵測。
- **模板即真檔**：範本存於 `templates/`，以 C++20 `#embed` 於編譯期嵌入 dcap 執行檔。
- **單檔 ≤150 行**：原始碼按職責分成多個小模組。
- **產物可攜**：dcap 與產生的專案皆 `-static` 靜態連結，獨立可執行。
