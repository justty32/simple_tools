# dcap

極簡的 C / C++ 專案 scaffolder。把一個模板複製成新專案、替換 `@NAME@`、並 `git init`。

產生的專案是**兩個檔**：`bin/<name>` 是執行檔，`lib/` 底下是 shared library。進入點就是普通的 `int main()`，平台是 Linux / macOS / Windows 都能跑。

本體：沒有任何 shell 腳本（內嵌檔案在 C++ 裡一個個手寫 `#embed`）、用 CMake、沒有 clang-format、沒有 agent 工作流、模板只留必要的檔。

## 唯一指令

```
dcap <template> <name>
```

- `<template>`：路徑式模板、具名外部模板、或內建模板（`c` / `cpp`，永遠可用）。
- `<name>`：要建立的新專案目錄名。
- 行為：把解析到的模板目錄底下的東西全部原樣（逐位元組）複製成 `./<name>/`，只在新專案的 `CMakeLists.txt` 內把 `@NAME@` 替換成 `<name>`（其他檔案內容與所有檔名都不變），再對新專案執行 `git init`。
- 參數不足 → 印 usage 到 stderr 並回傳 1。`<name>` 已存在 → 報錯，不覆蓋。沒有子指令、沒有 help 指令。

## 模板來源（`argv[1]` 的解析順序）

1. **路徑式**：`argv[1]` 開頭是 `.` 或 `/`（如 `./x`、`../x`、`/abs/y`）→ 視為路徑。注意像 `a/b` 這種「含 `/` 但不以 `.` 或 `/` 開頭」的**不是**路徑式，會被當裸名。
2. **具名外部**：設了環境變數 `DCAP_TEMPLATES` 時，裸名會先去 `$DCAP_TEMPLATES/<名>` 找；找到就用它，因此在那裡放一個與內建同名的模板會**蓋掉那個內建模板**。
3. **內建**：以 C++20 `#embed` 編進執行檔，永遠可用，不需任何環境變數。

合法模板的判定 = 該目錄底下有 **`CMakeLists.txt`**。找不到目錄或缺 `CMakeLists.txt` → 報錯。

### 自己加一個內建模板

**沒有掃目錄的產生器**，要動 C++。`#embed` 是 preprocessor directive，包不進 macro，所以每個檔案都得自己一段。在 [`src/builtin.cpp`](src/builtin.cpp) 加一個 namespace，每個檔一個 `#embed` 區塊 + 一筆 `kFiles`，再往 `kTemplates` 加一列：

```cpp
namespace tpl_clib {
constexpr unsigned char cmakelists[] = {
#embed "../templates/clib/CMakeLists.txt"
, 0};
const File kFiles[] = {
    {"CMakeLists.txt", bytes(cmakelists, sizeof cmakelists - 1)},
};
}  // namespace tpl_clib
```

`#embed` 的路徑相對於 `builtin.cpp`。陣列尾巴那個 `0` 是為了讓空檔案也還是合法的 initialiser，長度用 `sizeof - 1` 取，所以那個 0 不會混進內容。用 `unsigned char` 是因為模板檔是 UTF-8，超過 127 的位元組塞不進 `char`。

不想動 C++ 就走 `DCAP_TEMPLATES` 外部模板，行為完全一樣。

## 建置 dcap 本體

需要支援 C++23 的編譯器：本體同時用了 `#embed`（嵌內建模板）跟 `std::expected`
（錯誤處理），兩個都要——GCC 15+、Clang 19+，而且**標準函式庫也要跟上**（編譯器
版本夠新不代表附帶的 libstdc++/libc++ 也支援 `std::expected`）。另外要 CMake
3.20+。

```sh
cmake -B build && cmake --build build      # 產出 bin/dcap
```

執行檔固定落在 `bin/`，所以 PATH 設一次就不用再改。改了 `templates/` 底下的檔會自動重建——編譯器會把 `#embed` 的檔案寫進 depfile，CMake 照著走，**不需要在 CMakeLists 裡列模板檔**。

注意這是**本體**的需求。`#embed` 跟 `std::expected` 只有本體用到，**產生的專案不受影響、仍然只需要 C++20**——用什麼舊編譯器建生出來的專案都行。

## 安裝

自行把本 repo 的 `bin/` 加入 PATH。在 `~/.bashrc` 或 `~/.zshrc` 加入（**用絕對路徑**）：

```sh
export PATH="/絕對路徑/dcap/bin:$PATH"
```

沒有 install.sh、不 copy 到系統目錄。

## 快速上手

```sh
dcap cpp hello                              # 用內建 cpp 模板建立 ./hello（並 git init）
cd hello && cmake -B build && cmake --build build
./bin/hello                                 # → 2 + 3 = 5

dcap c mytool                               # 內建 C 模板
dcap ./my-template proj                     # 用當前目錄下的 my-template（需含 CMakeLists.txt）
DCAP_TEMPLATES=~/tpls dcap web api          # 用 ~/tpls/web 模板
```

不想要 `build/` 的話 in-source 也可以，產物位置一樣：

```sh
cmake . && cmake --build . && ./bin/hello
```

## 產生後的專案

產物佈局、target 命名、本地專案互相引用、第三方函式庫與跨平台 caveat 集中在
[`PROJECTS.md`](PROJECTS.md)。

## 給 agent 用

要把 dcap 包成 LLM 能叫的工具，看 [`tool/README.md`](tool/README.md)；機器可讀的
規格（schema、`argv` 對應方式）在 [`tool/dcap.json`](tool/dcap.json)。

## 授權

自用工具，未附授權條款。
