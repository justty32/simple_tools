# 設計說明

← [文件首頁](index.md)

## 模組結構（每檔 ≤150 行）

原本規範的「單檔 main.cpp」拆成多個小模組，各司其職：

| 檔 | 職責 |
|----|------|
| `src/main.cpp` | 進入點；解析 `argv[1]` 並派發到各指令 |
| `src/commands.{hpp,cpp}` | `cmd_new()`、`cmd_build()`、`print_usage()` |
| `src/templates.{hpp,cpp}` | 以 `#embed` 嵌入 `templates/` 的範本並替換 `@NAME@` |
| `src/platform.{hpp,cpp}` | `make_program()`、`exe_suffix()`、`dcap_home()`、`run()` |
| `src/util.{hpp,cpp}` | `write_file()`、`ensure_dir()`、`path_exists()` |

所有 OS 專屬分支都收在 `platform.*`，其餘程式碼保持平台中立。

## 模板嵌入：`#embed`

範本不寫成 C++ 字串字面值，而是存成 `templates/` 下的**真實檔案**（可用一般編輯器編輯、語法highlight）：

```
templates/
  cpp/Makefile   cpp/main.cpp
  c/Makefile     c/main.c
  gitignore
```

`src/templates.cpp` 於編譯期用 C++20 的 `#embed`（GCC 15+）把每個檔案嵌入成 `constexpr char[]`：

```cpp
constexpr char kCppMakefile[] = {
#embed "../templates/cpp/Makefile"
    , 0};   // 尾端補 0 → C 字串
```

執行期把範本裡的 `@NAME@` 佔位符替換成專案名。好處：範本與程式碼分離、可直接檢視 diff、無需額外的 codegen 工具（只靠 g++）。

## 跨平台策略

保留 UNIX 慣例字串，於**執行期**偵測平台：

| 面向 | 作法 |
|------|------|
| make 程式 | Windows 先試 `mingw32-make`，否則 `make`（反之亦然）|
| 產物副檔名 | `exe_suffix()` → `.exe` / 空；`find_binary()` 以 `std::filesystem` 找實際檔 |
| 共用 lib 路徑 | `DCAP_HOME` 環境變數 + 產生的 Makefile 內 `ifeq ($(OS),Windows_NT)` 後援 |
| 執行子程序 | `std::system`，呼叫前先 `flush` 自身 stdout 避免輸出交錯 |

## 靜態連結

dcap 本體與產生的專案都用 `-static`：

- dcap.exe 不依賴 `libstdc++-6.dll` / `libgcc_s_seh-1.dll`，可獨立散布。
- 產生的小工具也是單一可執行檔，方便隨手丟到任何地方跑。

## 產生的 Makefile 為何用 `find`

規範要求「自動搜尋所有原始碼」。採用 `$(shell find . -name '*.cpp')` 做**無限遞迴**，新增檔案不用改 Makefile。Windows 上依賴 Git 附帶的 `find`/`sh`（見 [使用指南 › 疑難排解](usage.md)）。

## 建置本體

`Makefile` 用 `g++ -std=c++20`，`SRC := $(wildcard src/*.cpp)`（本體原始碼都在單層 `src/`）。`templates/*` 列為相依，改範本會觸發重編（因為它們被 `#embed` 進來）。
