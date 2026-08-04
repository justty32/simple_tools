# dcap — 工作筆記

跨 session 的決定。**只記不看 code 看不出來的決定**；已經寫在 README 裡的東西不重複抄。

## CMakeLists 設計決策

**沒有 install rule 了**（連同 `INSTALL_RPATH`、`GNUInstallDirs` 一起拿掉）。產生的專案只管建出 `bin/` 和 `lib/` 那兩個產物，安裝／散布交給使用者。

實作上這幾樣別當成多餘：

- **`if(PROJECT_IS_TOP_LEVEL)`** 包住執行檔 —— AAA 被 `add_subdirectory` 進來時只該建 library，不該建它自己的執行檔。
- **`if(WIN32)` 那段 `TARGET_RUNTIME_DLLS` 複製** —— Windows 沒有 RPATH，被連結專案的 dll 在它自己的目錄，loader 不會去找。Linux/macOS 不需要（CMake 自動寫 RUNPATH，實測 bbb 的 RUNPATH 同時含 bbb 和 aaa 兩個 `lib/`）。
- **`cmake_minimum_required` 是 3.21 而不是 3.20** —— 上面那兩個都是 3.21 才有的。
- **模板有 `CMAKE_BUILD_TYPE` 預設 Release 的三行** —— 第二版的 README 寫了「預設是 Release」但模板根本沒設，是個文件／程式碼不符；這一版讓它成真。

已知的兩個銳角，**都是刻意不修、寫進 README 的**：

- `src/` 至少要有一個 `main.*` 以外的檔，否則 library 沒原始碼、CMake 報 `No SOURCES given to target`。模板附的 `lib.*` 就是那個檔。
- 輸出目錄寫死成 `bin/` 和 `lib/`，所以要用**單組態產生器**（Ninja / Unix Makefiles / MinGW Makefiles）。Visual Studio 那種多組態產生器會再加一層，變成 `bin/Release/`。

**明確的非目標：第三方相依。** CMake 原生就夠好（GitHub 抓的用 `FetchContent`／`add_subdirectory`，apt/brew 裝的用 `find_package`／`pkg_check_modules`），模板多寫任何一段都只是猜使用者要哪一種。**不要好心加。**

## 慣例

- **commit 直接進 `main`**，不開分支。**push 要先問過。**（原始出處是原版的 `AGENTS.md`，移到這裡是因為第二版之後刻意沒有 AGENTS.md。）
- 重構／整理**不改變原意**，改完要跑驗證。**無 lint、無 test，只有實跑。** 驗證是：`cmake -B build && cmake --build build` 建本體 → `dcap cpp demo` → `cd demo && cmake -B build && cmake --build build && ./bin/demo` 應印出 `2 + 3 = 5`；C 模板同樣跑一次；再驗 in-source（`cmake . && cmake --build .`）與 `add_subdirectory` 組合。

### 這台機器上怎麼驗（2026-08-04 實測過）

Windows 11 + WSL2，兩條路徑都驗得到，不用另外找 Linux 機器：

- **Windows 原生**：`C:\dev\mingw64` 的 g++ **16.1.0 有 `#embed`**，所以本體建得起來（`cmake -B build -G "MinGW Makefiles"`）。驗 PE/dll 那條路徑。
- **WSL2 Ubuntu**：gcc 只有 13.3，**沒有 `#embed`，本體在那邊建不起來**——但**產生的專案不需要 `#embed`**，所以把 Windows 產出的專案目錄複製進 `~/`（記得先清掉 `build/`、`bin/`、`lib/`、`CMakeCache.txt`）再建，就驗到 ELF/`.so` 那條路徑。cmake 3.28，夠 3.21。
- 沒有 symlink 了，所以第二版那條「WSL 不能放 `/mnt/c`」的限制**已經不存在**。
- **VS Code 的 clangd 會佔住專案目錄**，重新命名或刪掉整個資料夾時會 `Permission denied`；先關掉編輯器或結束 clangd 行程。
