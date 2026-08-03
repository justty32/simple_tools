# 04 — 部署、驗證、風險與決策

← [plan 索引](README.md)

## 部署 / 安裝

- **UNIX**：`install.sh`
  ```sh
  make            # 產出 bin/dcap
  sudo make install   # cp bin/dcap /usr/local/bin/（或提示加入 PATH）
  ```
- **Windows**：用 `mingw32-make` 產出 `bin\dcap.exe`，把 `bin` 加入 PATH，或複製到 `C:\dev\mingw64\bin`。

## 驗證步驟（端對端）

1. 建置本體：`mingw32-make`（產出 `bin/dcap.exe`）。
2. 在暫存目錄：
   - `dcap new demo` → 檔案齊全、`git` 已初始化。
   - `cd demo && dcap build` → 編譯並印出 `Hello, World!`。
   - `dcap new-c democ` → `cd democ && dcap build` → 同上（C 版）。
   - 空目錄 `dcap build` → 應報錯。
   - 未知指令 / `dcap help` → 檢查輸出。
3. 修正錯誤直到全綠。

## 風險

- **R2**：`make -j4` 在極少數 MinGW 設定下平行度受限；不影響正確性。
- **R3**：`$(DCAP_HOME)/cpp_libs`、`c_libs` 可能不存在 → `-I` 指向不存在目錄，g++ 忽略，無害。
- **R4**：「安裝到 `/usr/local/bin`」在 Windows 無意義 → 以 PATH 說明替代。
- **R5**：Windows 上 `find` 需為 Git 附帶的 `usr/bin/find.exe`（非 System32 版）。若 mingw32-make 的 shell 找到 System32 版 find，`$(shell find ...)` 會失敗。
  - 緩解：實作時驗證；README 提醒 Windows 使用者把 Git `usr/bin` 置於 PATH 前段，或於 Git Bash 執行。必要時提供 `wildcard` 後援。

## 已確認決策

1. 名稱：**dcap** ✅
2. 跨平台：保留 UNIX 字串 + 執行期偵測 ✅
3. Makefile 檔案搜尋：`find` 無限遞迴 ✅
4. 不使用 CMake，本體用 Makefile + g++ ✅
5. 共用 lib 路徑：環境變數 `DCAP_HOME`（預設 `~/dev/dcap`、`C:/dev/dcap`）✅
6. 單檔 ≤150 行，多模組 ✅
7. `dcap build` 不傳額外參數給子專案（可再議）
