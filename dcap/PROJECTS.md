# dcap 產生的專案

本頁說明 dcap 模板產出的 CMake 專案。CLI、模板來源與 dcap 本體建置見
[`README.md`](README.md)。

## 兩個產物

`cmake --build build` 會把產物放在專案根目錄的 `bin/` 和 `lib/`，不放在 `build/`：

| 位置 | 是什麼 |
|---|---|
| `bin/<name>`（Windows 是 `bin\<name>.exe`） | 執行檔，`src/main.cpp` 是進入點 |
| `lib/lib<name>.so` / `.dylib`（Windows 是 `lib\` 的 import library + `bin\` 的 dll） | shared library，`src/` 其餘檔案都在裡面 |

實際檔名交給 CMake。Windows 的 dll 跟執行檔一起放 `bin/`，`lib/` 放 import library。
Linux/macOS 由 CMake 寫入 RUNPATH，Windows 則因 dll 在旁邊，所以 `bin/<name>` 從任何 cwd
都能執行。

`src/main.cpp` 是唯一有特殊意義的檔名：只編進執行檔，不進 library。`src/` 其餘檔案
（含子目錄）全部進 library。`src/` 至少要有一個 `main.cpp` 以外的檔，否則 CMake 會報
`No SOURCES given to target`；模板附的 `src/lib.cpp` 就是那個檔。

library target 叫 `<name>`，執行檔 target 叫 `<name>_main`，但後者產出的檔案仍叫 `<name>`。

## 引用另一個本地 dcap 專案

專案就是資料夾，不必先安裝，也不用 `find_package`。在使用端 BBB 的 CMakeLists 尾端加入：

```cmake
add_subdirectory(/path/to/AAA AAA-build)
target_link_libraries(BBB PRIVATE AAA)
```

相對路徑（`../AAA`）也可以。第二個參數是外部 source tree 對應的 build 目錄。
AAA 的 `include/`、實際 library 檔名和建置依賴都會由 CMake 傳遞。AAA 被引用時只建 library，
不建自己的 `bin/AAA`；模板用 `if(PROJECT_IS_TOP_LEVEL)` 控制這件事。

## 專案佈局與設定

`include/` 放公開標頭，`src/` 放原始碼。`src/` 使用帶 `CONFIGURE_DEPENDS` 的 glob，新增檔案後
直接執行 `cmake --build build` 即可，不用手動重跑 configure 或編輯 CMakeLists。

模板沒有預建 `test/` 或格式化設定。Debug／Release 使用 CMake 原生選項：

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug     # 預設是 Release
```

第三方函式庫也交給 CMake：Git repository 可用 `FetchContent` 或 `add_subdirectory`，系統套件
用 `find_package` 或 `pkg_check_modules`。

## 平台

產生的專案支援 Linux、macOS、Windows，要求 CMake 3.21+（`PROJECT_IS_TOP_LEVEL` 和
`TARGET_RUNTIME_DLLS` 從 3.21 起提供）。檔名、符號匯出與 runtime library 位置都由 CMake
處理；Windows 使用 `WINDOWS_EXPORT_ALL_SYMBOLS` 並把相依 dll 複製到 `bin/`。

模板把輸出目錄固定為 `bin/` 和 `lib/`，因此請使用單組態產生器，例如 Ninja、Unix
Makefiles 或 MinGW Makefiles。Visual Studio 等多組態產生器會再加入一層，成為
`bin/Release/`。
