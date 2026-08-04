# simple_tools — 工作筆記

跨 session 的決定與待辦。**只記還沒完的事和不看 code 看不出來的決定**；已經寫在各專案 README 裡的東西不重複抄。

## dcap

唯一的專案：[`dcap/`](dcap/README.md)。

它其實是第三版。前兩版——原版（Makefile + `tools/gen-embed.sh` 掃 `templates/` 產生內建模板登錄表）和第二版（改用 CMake、手寫 `#embed`、產物是「單一可執行的 .so」）——**2026-08-04 從工作目錄刪掉了**，內容留在 git 歷史：

```sh
git show 0be6364:dcap/README.md      # 原版
git show 0be6364:dcap-2/README.md    # 第二版
```

下面的約束是那兩版一路累積下來的，現在全部適用於 `dcap/`。

### 一路繼承的設計約束（2026-08-03 定的，都是取捨不是疏漏）

使用者當時的原話：完全捨棄 shell 腳本、內嵌檔案單檔一個個指定（不用 shell 產生）、放棄 Makefile 改用 CMake（本體與產生的專案都是）、**「完全的精簡」**、不要 clang-format、拿掉 agent 工作流。

「完全的精簡」講了兩次而且很堅決 —— 這是為什麼沒有 `test/`、沒有 `fmt` target、沒有 `docs/`、沒有 `wf/`，以及四個 scaffolder 模組被併成一個 `main.cpp`。**不要好心加回來。**（`install` target 原本也在這份被砍的名單上，後來使用者要求加回，見下。）

反過來說，`c` 和 `cpp` 兩個內建模板是**刻意留下**的（使用者被問到時明確選擇保留），別當成沒精簡到。

第二版另外有幾樣東西純粹是為了「單一產物既可執行又可連結」那個把戲而存在的——`lib<name>.so` symlink（soname 沒有它解不開，連結時和執行時都要）、`readelf` 偵測 loader 路徑、以及 ELF-only 的立場。**這一版把那個把戲整個拿掉，這幾樣也就跟著消失了。**

### 這一版的設計決定（2026-08-04 定的）

做的事：**把產物拆成執行檔 + shared library 兩個檔**。連帶消失的是 `.interp`／PT_INTERP、`-Wl,-e`、`-Wl,-soname`、`NO_SONAME`、`readelf` 偵測、`lib<name>.so` symlink，以及進入點那三條彆扭規則（不能 return、沒有 argc/argv、必須 `force_align_arg_pointer`）。進入點回到普通的 `int main()`。跨平台在第二版本來是「結案、不打算修」的，**這一版重開了這一局**——擋在前面的一直是那個把戲。

使用者對模板講明的兩個用途：(1) `cmake . && cmake --build .` 之後直接跑得起來（所以產物不埋在 `build/` 裡，**in-source build 也要能用**）；(2) 另一個專案能用路徑 include 這個專案**並連結它**。

被問到時明確做的選擇：

| 決定 | 為什麼 |
|---|---|
| 永遠產兩個檔（不做「.so 可選」開關） | 用途 (2) 是常態，不該要多一個 flag |
| **正式宣告跨平台**（Linux / macOS / Windows） | 擋在前面的一直是那個把戲，不是 make 也不是 CMake |
| `src/main.*` 是進入點，`src/` 其餘全部進 library | 維持「新增檔案直接 build 就編進去」的性質，不用開 `app/` 目錄 |
| 跨專案引用用 `add_subdirectory` + 目標名 | `libAAA.so` 這種檔名在 Windows/macOS 不成立。改成 target 之後 include 自動傳遞、檔名交給 CMake、AAA 還會被一起建起來 —— 目標名取 `<name>` 防撞名的設計到這裡才真正用上 |

**產物位置與執行檔名，2026-08-04 稍晚被使用者改過一次**，理由是「遵從慣例」——原本沿用第二版的「產物放專案根、執行檔叫 `main`」，改成：

- **執行檔 → `bin/<name>`，library → `lib/`**。專案佈局因此跟安裝目的地同構。
- Windows 例外：**dll 跟執行檔一起放 `bin/`**，`lib/` 只放 import library。這不是不一致，是該平台 loader 只往 exe 旁邊找；跟 CMake `install()` 的 `RUNTIME`／`ARCHIVE` 預設分法一樣。
- **執行檔改叫 `<name>`（不再是 `main`）**。上一版叫 `main` 是因為那個檔同時是 `.so`、不能叫 `lib<name>.so`；拆開又分目錄之後這個理由沒了，`bin/demo` + `lib/libdemo.so` 一看就是一對。注意**執行檔的 target 仍叫 `<name>_main`**，因為 library target 已經佔用了 `<name>`——只有 `OUTPUT_NAME` 是 `<name>`。
- 附帶好處：模板的 `.gitignore` 從一長串副檔名（`*.so` `*.dll` `*.dylib` `*.a` `*.lib` `*.exp` `*.pdb`…）縮成 `bin/` + `lib/`。

**install target：使用者要求加回來（2026-08-04）。** 這一條**推翻了上面「完全的精簡」砍掉的其中一項**，是使用者明確要的，不是有人手滑加回去的。做法刻意壓到最小：`include(GNUInstallDirs)` + `install(TARGETS)` + `install(DIRECTORY include/)`，沒有 export set、沒有 `<name>Config.cmake`、沒有版本檔——跨專案引用走的是 `add_subdirectory`，本來就不需要 `find_package`。三個實作細節別當成多餘：

- **library 和 headers 的 install rule 放在 `if(PROJECT_IS_TOP_LEVEL)` 外面**，執行檔的放裡面。被 `add_subdirectory` 引用進來的專案必須跟著裝，不然裝出去的執行檔載不到它的 library。
- **`INSTALL_RPATH` 寫成相對路徑**（`$ORIGIN/../${CMAKE_INSTALL_LIBDIR}`），裝好的執行檔才不用 `LD_LIBRARY_PATH`，整個 prefix 也才能整包搬走。CMake 預設會把 build-tree RPATH 剝掉、install RPATH 留空，不設就是壞的。
- 那個字串同時塞 `$ORIGIN` 和 `@loader_path` 兩種寫法（ELF 一種、Mach-O 一種），**是為了不寫 `if(APPLE)`**；不適用的那個只是個不存在的路徑，無害。Windows 兩個都不需要，dll 本來就跟執行檔一起裝進 `bin/`。

實作上這幾樣也別當成多餘：

- **`if(PROJECT_IS_TOP_LEVEL)`** 包住執行檔 —— AAA 被 `add_subdirectory` 進來時只該建 library，不該建它自己的執行檔。
- **`if(WIN32)` 那段 `TARGET_RUNTIME_DLLS` 複製** —— Windows 沒有 RPATH，被連結專案的 dll 在它自己的目錄，loader 不會去找。Linux/macOS 不需要（CMake 自動寫 RUNPATH，實測 bbb 的 RUNPATH 同時含 bbb 和 aaa 兩個 `lib/`）。
- **`cmake_minimum_required` 是 3.21 而不是 3.20** —— 上面那兩個都是 3.21 才有的。
- **模板有 `CMAKE_BUILD_TYPE` 預設 Release 的三行** —— 第二版的 README 寫了「預設是 Release」但模板根本沒設，是個文件／程式碼不符；這一版讓它成真。

已知的三個銳角，**都是刻意不修、寫進 README 的**：

- `src/` 至少要有一個 `main.*` 以外的檔，否則 library 沒原始碼、CMake 報 `No SOURCES given to target`。模板附的 `lib.*` 就是那個檔。
- 輸出目錄寫死成 `bin/` 和 `lib/`，所以要用**單組態產生器**（Ninja / Unix Makefiles / MinGW Makefiles）。Visual Studio 那種多組態產生器會再加一層，變成 `bin/Release/`。
- install 時 headers 是平鋪進 `<prefix>/include/`，兩個組在一起的專案若有同名標頭（例如都留著模板的 `lib.hpp`）會互相覆蓋。不打算為此加 per-project 子目錄——那會讓單一專案的 `#include "lib.hpp"` 也要跟著改。

**明確的非目標：第三方相依。** CMake 原生就夠好（GitHub 抓的用 `FetchContent`／`add_subdirectory`，apt/brew 裝的用 `find_package`／`pkg_check_modules`），模板多寫任何一段都只是猜使用者要哪一種。**不要好心加。**

## 慣例

- **commit 直接進 `main`**，不開分支。**push 要先問過。**（原始出處是原版的 `dcap/AGENTS.md`，但第二版之後刻意沒有 AGENTS.md，所以記在這裡。）
- 重構／整理**不改變原意**，改完要跑驗證。**無 lint、無 test，只有實跑。** 驗證是：`cmake -B build && cmake --build build` 建本體 → `dcap cpp demo` → `cd demo && cmake -B build && cmake --build build && ./bin/demo` 應印出 `2 + 3 = 5`；C 模板同樣跑一次；再驗 in-source（`cmake . && cmake --build .`）、`add_subdirectory` 組合、以及 `cmake --install`。

### 這台機器上怎麼驗（2026-08-04 實測過）

Windows 11 + WSL2，兩條路徑都驗得到，不用另外找 Linux 機器：

- **Windows 原生**：`C:\dev\mingw64` 的 g++ **16.1.0 有 `#embed`**，所以本體建得起來（`cmake -B build -G "MinGW Makefiles"`）。驗 PE/dll 那條路徑。
- **WSL2 Ubuntu**：gcc 只有 13.3，**沒有 `#embed`，本體在那邊建不起來**——但**產生的專案不需要 `#embed`**，所以把 Windows 產出的專案目錄複製進 `~/`（記得先清掉 `build/`、`bin/`、`lib/`、`CMakeCache.txt`）再建，就驗到 ELF/`.so` 那條路徑。cmake 3.28，夠 3.21。
- 沒有 symlink 了，所以第二版那條「WSL 不能放 `/mnt/c`」的限制**已經不存在**。
- **VS Code 的 clangd 會佔住專案目錄**，重新命名或刪掉整個資料夾時會 `Permission denied`；先關掉編輯器或結束 clangd 行程。
