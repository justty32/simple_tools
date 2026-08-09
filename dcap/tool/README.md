# dcap 給 agent 用的說明

dcap 是一個 C/C++ 專案的 scaffolder。它做三件事：把一個模板目錄複製成新專案、
把新專案 `CMakeLists.txt` 裡的 `@NAME@` 換成專案名稱、對新專案跑 `git init`。
不建置、不裝套件、不管相依，就只是「生一個能建的空專案」。

## 指令

```
dcap <template> <name>
```

只有兩個位置參數，順序固定：

- `<template>`：用哪個模板。內建的是 `c` 或 `cpp`，永遠可用。也可以是一個模板
  目錄的路徑（`./x`、`../x`、`/abs/x`，該目錄底下要有 `CMakeLists.txt`）；或者
  設了 `DCAP_TEMPLATES` 環境變數時，該目錄底下的具名模板（同名會蓋掉內建的）。
- `<name>`：新專案的資料夾名稱，會建立在目前目錄下（`./<name>`）。

機器可讀的完整規格（含每個參數的 JSON schema、argv 對應方式、`exec` 的絕對
路徑）在同目錄的 [`dcap.json`](dcap.json)。

兩個關於 `dcap.json` 的注意事項：`exec` 指的 `bin/dcap` **不在 git 裡**，clone
下來要先自己建一次（見根目錄 README）；`source` 那段是建置當下的檔案指紋，重建
過就會對不上，那只是「規格可能過期了」的標記，不影響工具能不能跑。

## 產生出來的專案長什麼樣

建置後（見下）會有**兩個產物**：`bin/<name>` 是執行檔，`lib/lib<name>.so`
（macOS 是 `.dylib`，Windows 是 dll + import library）是 shared library。

- `src/main.cpp` 是唯一有特殊意義的檔名，只會編進執行檔，是程式的進入點。
- `src/` 底下其他檔案（含子目錄）全部編進 library，沒有命名規則。
- 這代表 **`src/` 至少要保留一個 `main.cpp` 以外的檔**——library 沒有原始碼的
  話，CMake 會在 configure 時報錯（`No SOURCES given to target`）。模板附的
  `src/lib.cpp`（或 `src/lib.c`）就是留著給 library 用的那個檔，不要刪光。

## 怎麼建置、怎麼跑

```sh
cd <name>
cmake -B build && cmake --build build
./bin/<name>
```

用 in-source build 也可以（`cmake . && cmake --build .`），產物位置不變。
必須用單組態產生器（Ninja、Unix Makefiles），不能用 Visual Studio 那種多組態的
——輸出目錄寫死在 `bin/`、`lib/`，多組態產生器會多包一層 `bin/Release/`。

## 失敗時會怎樣

失敗一律：exit code 1，錯誤訊息印在 stderr、開頭是 `[dcap] error: `。
成功訊息在 stdout，開頭是 `[dcap] `。**`<name>` 已存在時一定失敗，不會覆蓋、
不會合併**——這是安全上最重要的一條：呼叫前如果不確定 `<name>` 存不存在，
先自己檢查，不要指望 dcap 幫你避開。

## 不該拿 dcap 做什麼

- **不是建置工具**：dcap 跑完就結束了，它不會幫你 `cmake --build`，那是另一步。
- **不是套件管理器**：不會幫你抓、裝任何函式庫。
- **不管第三方相依**：產生的 `CMakeLists.txt` 完全沒碰這件事，要用
  `FetchContent`、`find_package` 之類的，自己加到專案的 `CMakeLists.txt`。
- 沒有 subcommand、沒有 `--help`：唯一介面就是這兩個位置參數。

深入的細節（模板解析順序、跨平台的差異、怎麼加新的內建模板、怎麼串兩個 dcap
專案）看 repo 根目錄的 [`README.md`](../README.md)。
