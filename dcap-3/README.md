# dcap-3

極簡的 C / C++ 專案 scaffolder。把一個模板複製成新專案、替換 `@NAME@`、並 `git init`。

與 [dcap-2](../dcap-2/README.md) 的差別只有一件事，但影響很大：**產生的專案是兩個檔**——`bin/<name>` 是執行檔，`lib/` 底下是 shared library。dcap-2 那個「單一產物既可執行又是 `.so`」的把戲拿掉了，隨之消失的是：ELF 專屬的 `.interp`／PT_INTERP／soname／symlink、`readelf` 偵測、以及進入點那三條彆扭規則。**進入點就是普通的 `int main()`，平台就是 Linux / macOS / Windows 都能跑。**

本體本身跟 dcap-2 一樣：沒有任何 shell 腳本（內嵌檔案在 C++ 裡一個個手寫 `#embed`）、用 CMake、沒有 clang-format、沒有 agent 工作流、模板只留必要的檔。

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

## 建置 dcap-3 本體

需要支援 C++20 `#embed` 的編譯器（GCC 15+、Clang 19+）、CMake 3.20+。

```sh
cmake -B build && cmake --build build      # 產出 bin/dcap
```

執行檔固定落在 `bin/`，所以 PATH 設一次就不用再改。改了 `templates/` 底下的檔會自動重建——編譯器會把 `#embed` 的檔案寫進 depfile，CMake 照著走，**不需要在 CMakeLists 裡列模板檔**。

注意這是**本體**的需求。`#embed` 只有本體用到，**產生的專案不需要它**——用什麼舊編譯器建都行。

## 安裝

自行把本 repo 的 `bin/` 加入 PATH。在 `~/.bashrc` 或 `~/.zshrc` 加入（**用絕對路徑**）：

```sh
export PATH="/絕對路徑/dcap-3/bin:$PATH"
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

## 產生的專案：兩個檔

`cmake --build build` 出來的是**兩個產物**，而且都不在 `build/` 裡，是照慣例分在 `bin/` 和 `lib/`：

| 位置 | 是什麼 |
|---|---|
| `bin/<name>`（Windows 是 `bin\<name>.exe`） | 執行檔，`src/main.cpp` 是它的進入點 |
| `lib/lib<name>.so` / `.dylib`（Windows 是 `lib\` 的 import library + `bin\` 的 dll） | shared library，`src/` 其餘全部在裡面 |

實際檔名交給 CMake，不用記：Linux 是 `lib/lib<name>.so`、macOS 是 `lib/lib<name>.dylib`。Windows 照該平台的慣例擺——**dll 跟執行檔一起放 `bin/`**（loader 只往那裡找），`lib/` 放 import library。

執行檔連結那個 library，所以 `./bin/<name>` 在任何 cwd 都直接跑（Linux/macOS 靠 CMake 自動寫進去的 RUNPATH，Windows 靠 dll 就在旁邊）。

**`src/main.cpp` 是唯一有特殊意義的檔名**——它只編進執行檔，不進 library。`src/` 底下其他的檔（含子目錄）全部進 library，沒有命名規則。反過來說 **`src/` 至少要有一個 `main.cpp` 以外的檔**，不然 library 沒有原始碼，CMake 會在 configure 時報 `No SOURCES given to target`。模板附的 `src/lib.cpp` 就是那個檔，別把它刪光。

library target 的名字就是 `<name>`（不是 `lib` 之類的通名），執行檔 target 叫 `<name>_main`（它產出的**檔案**才是 `<name>`）；兩個專案組在一起時才不會撞 target 名。

### 引用另一個本地 dcap 專案

專案就是資料夾——不必先安裝、不用 `find_package`。在**用**的那邊（BBB）CMakeLists 尾巴加兩行，直接指路徑：

```cmake
add_subdirectory(/path/to/AAA AAA-build)
target_link_libraries(BBB PRIVATE AAA)
```

相對路徑（`../AAA`）也可以。`add_subdirectory` 的第二個參數是 build 目錄名，因為 AAA 在 BBB 的樹外面，CMake 要求給。

這樣寫的好處是三件事都自動了：AAA 的 `include/` 會跟著傳過來（不用自己加 `target_include_directories`）、library 的實際檔名交給 CMake（所以三個平台都對）、**AAA 會被一起建起來**（不必先手動 build AAA）。

AAA 被這樣引用時**只會建它的 library**，不會建它自己那個 `bin/AAA`——模板裡的 `if(PROJECT_IS_TOP_LEVEL)` 擋掉了。

### 專案佈局與指令

`include/` 放公開標頭、`src/` 放原始碼。`src/` 底下的檔案是 glob 進去的，而且帶 `CONFIGURE_DEPENDS`，所以**新增原始碼直接 `cmake --build build` 就會編進去**，不用手動重跑 configure、也不用改 CMakeLists。**沒有 test/、沒有 fmt**——debug/release 用 CMake 原生的：

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug     # 預設是 Release
```

### 安裝

有 install rule，用 CMake 原生的指令，不需要 `sudo` 也不需要腳本：

```sh
cmake --install build --prefix ~/.local     # 不給 --prefix 就是 CMake 預設的 /usr/local
```

裝出來的東西（`GNUInstallDirs`，所以跟專案自己的佈局一樣）：

```
~/.local/bin/<name>              執行檔
~/.local/lib/lib<name>.so        library（Windows 是 bin\<name>.dll + lib\ 的 import library）
~/.local/include/…               include/ 底下的東西原樣複製過去
```

裝好的執行檔**不需要 `LD_LIBRARY_PATH`**：模板給它設了相對的 `INSTALL_RPATH`（`$ORIGIN/../lib`，macOS 是 `@loader_path/../lib`），所以整個 prefix 搬到別的地方也還是能跑。

用 `add_subdirectory` 引用進來的專案**會跟著一起裝**（它的 library 和 headers），不然裝出來的執行檔會找不到東西可以載入。注意 headers 是**平鋪**進 `<prefix>/include/`，所以兩個專案如果有同名的標頭（比如都留著模板附的 `lib.hpp`），後裝的會蓋掉先裝的——要一起安裝就把標頭改成不會撞的名字。

in-source build（`cmake .`）的話 build 目錄就是專案根：`cmake --install . --prefix ~/.local`。

### 第三方函式庫

模板**不管**這件事，是刻意的：CMake 原生就處理得夠好，多寫任何一段都只是猜你要哪一種。GitHub 上抓的用 `FetchContent` 或 `add_subdirectory`，apt / brew 裝的用 `find_package` 或 `pkg_check_modules`，自己在 CMakeLists 加就好。

## 平台

**Linux / macOS / Windows。** 產生的專案裡沒有任何 ELF 專屬的東西了：檔名、匯出符號（`WINDOWS_EXPORT_ALL_SYMBOLS`）、執行時找得到 library（Linux/macOS 是 RUNPATH，Windows 是 build 後把被引用專案的 dll 複製到 `bin/`）都交給 CMake。本體本身（`std::filesystem` + `#embed`）也沒有平台包袱。

一個 caveat：模板把輸出目錄寫死成 `bin/` 和 `lib/`，所以請用**單組態產生器**（Ninja、Unix Makefiles、MinGW Makefiles）。Visual Studio 這種多組態產生器會在後面再加一層，變成 `bin/Release/`。

## 授權

自用工具，未附授權條款。
