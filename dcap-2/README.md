# dcap-2

極簡的 **Linux/ELF** C / C++ 專案 scaffolder。把一個模板複製成新專案、替換 `@NAME@`、並 `git init`。

與 [dcap](../dcap/README.md) 的差別：**沒有任何 shell 腳本**（內嵌檔案在 C++ 裡一個個手寫 `#embed`）、**改用 CMake**（dcap-2 本體與產生的專案都是）、**沒有 clang-format**、**沒有 agent 工作流**、模板砍到只剩必要的檔。

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

這裡跟 dcap 不一樣：**沒有掃目錄的產生器**，要動 C++。`#embed` 是 preprocessor directive，包不進 macro，所以每個檔案都得自己一段。在 [`src/builtin.cpp`](src/builtin.cpp) 加一個 namespace，每個檔一個 `#embed` 區塊 + 一筆 `kFiles`，再往 `kTemplates` 加一列：

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

## 建置 dcap-2 本體

需要支援 C++20 `#embed` 的編譯器（GCC 15+）、CMake 3.20+。

```sh
cmake -B build && cmake --build build      # 產出 bin/dcap
```

執行檔固定落在 `bin/`，所以 PATH 設一次就不用再改。改了 `templates/` 底下的檔會自動重建——GCC 會把 `#embed` 的檔案寫進 depfile，CMake 照著走，**不需要在 CMakeLists 裡列模板檔**。

## 安裝

自行把本 repo 的 `bin/` 加入 PATH。在 `~/.bashrc` 或 `~/.zshrc` 加入（**用絕對路徑**）：

```sh
export PATH="/絕對路徑/dcap-2/bin:$PATH"
```

沒有 install.sh、不 copy 到系統目錄。

## 快速上手

```sh
dcap cpp hello                              # 用內建 cpp 模板建立 ./hello（並 git init）
cd hello && cmake -B build && cmake --build build
./build/main                                # → 2 + 3 = 5

dcap c mytool                               # 內建 C 模板
dcap ./my-template proj                     # 用當前目錄下的 my-template（需含 CMakeLists.txt）
DCAP_TEMPLATES=~/tpls dcap web api          # 用 ~/tpls/web 模板
```

## 產生的專案：一個檔案，三種用法

`cmake --build build` 出來的是**單一產物** `build/main`，它同時是可執行檔與 shared library：

```sh
./build/main                                        # 直接執行
g++ yours.cpp -Iinclude -Lbuild -l<name> -o yours   # 當函式庫連結
dlopen("/path/to/build/main", RTLD_NOW)             # 或動態載入
```

作法是把它以 `-shared -fPIC` 連結成 .so（`add_library(main SHARED ...)` 加上 `PREFIX ""` `SUFFIX ""` 讓檔名就是 `main`），再內嵌一個 `.interp` 段指向系統的 dynamic loader（PT_INTERP，CMake 用 `readelf -p .interp /bin/sh` 自動偵測，glibc/musl 皆可），並用 `-Wl,-e,dcap_main` 指定進入點、`-Wl,-soname,lib<name>.so` 讓它能被連結（`NO_SONAME TRUE` 是為了擋掉 CMake 自己那份 soname）。build 後順手做一個 `build/lib<name>.so → main` 的 symlink，`-l<name>` 才找得到。

偵測失敗時：`cmake -B build -DDCAP_INTERP=/lib64/ld-linux-x86-64.so.2`。

### 專案佈局與指令

`include/` 放公開標頭、`src/` 放**全部**原始碼（沒有命名規則，`src/main.cpp` 只是放進入點的地方；子目錄也可以）。`src/` 底下的檔案是 glob 進去的，而且帶 `CONFIGURE_DEPENDS`，所以**新增原始碼直接 `cmake --build build` 就會編進去**，不用手動重跑 configure、也不用改 CMakeLists。**沒有 test/、沒有 fmt、沒有 install target**——debug/release 用 CMake 原生的：

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug     # 預設是 Release
```

要安裝就自己來（就一個檔加一個 symlink）：

```sh
install -Dm755 build/main ~/.local/lib/lib<name>.so
ln -sf ../lib/lib<name>.so ~/.local/bin/<name>
```

### 寫進入點要注意

進入點 `dcap_main` 不是普通的 `main()`：

- **不能 return** — 沒有 crt0 接手，必須自己呼叫 `exit()`。
- **拿不到 argc/argv** — 需要的話讀 `/proc/self/cmdline`。
- **必須掛 `__attribute__((force_align_arg_pointer))`** — ELF entry point 進來時 RSP 是 16-byte 對齊，但函式 prologue 預期的是「call 之後」的 `RSP % 16 == 8`；差這 8 bytes 會讓任何對齊的 SSE 存取直接 segfault（例如 `std::cout << some_int`）。

其他函式照常寫，不受影響。

## 平台

**Linux / ELF / x86-64。** dcap-2 本體（`std::filesystem` + `#embed`）本身沒什麼平台包袱，但它**產生的專案**綁死在 ELF 上：`.interp`／PT_INTERP、`-Wl,-soname`、`readelf`、以及 `force_align_arg_pointer` 那條 x86-64 的 RSP 假設，在 macOS（Mach-O）或 Windows（PE）都不成立。換成 CMake 沒有改變這件事——擋在跨平台前面的從來不是 make，是那個「可執行的 .so」把戲。這是刻意的取捨，不打算修。

### WSL2

可以用：WSL2 是真的 Linux kernel + ELF loader + glibc，上面那套全部成立。但那是「在 Windows 上跑 Linux」，產物仍然是 ELF，Windows 端不能直接執行。兩個地雷：

- **專案別放在 `/mnt/c/...`**。build 最後那個 `cmake -E create_symlink` 在 drvfs 上會失敗（不支援建 symlink），`/mnt/c` 的權限模型也讓 exec bit 形同虛設。放在 WSL 自己的檔案系統（`~/` 底下）就沒事，也快很多。
- **ARM 版 Windows 的 WSL2 不行**：那是 aarch64，`force_align_arg_pointer` 是 x86 專屬 attribute，那條 RSP 假設不存在。

## 授權

自用工具，未附授權條款。
