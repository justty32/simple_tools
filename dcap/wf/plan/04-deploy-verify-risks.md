# 04 — 部署、驗證、風險與決策

← [plan 索引](README.md)

## 部署 / 安裝

**由使用者自行把本 repo 的 `bin/` 加入 PATH**（dcap 執行檔本身也是這樣用）。不做系統層安裝、無安裝腳本、不 copy 到系統目錄。

- 建置：`make`（產出 `bin/dcap`）。
- 在 `~/.bashrc` 或 `~/.zshrc` 加入 `export PATH="/絕對路徑/dcap/bin:$PATH"`（用絕對路徑，rc 載入時 `$PWD` 不適用）。

> Windows 使用者請自行在 Git Bash / MinGW 環境裡想辦法。

## 驗證步驟（端對端）

1. 建置本體：`make`（產出 `bin/dcap`）。
2. 在暫存目錄：
   - `dcap cpp demo` → 檔案齊全（Makefile / main.cpp / .gitignore）、`git` 已初始化。
   - `cd demo && make run` → 編譯並印出 `Hello, World! (C++)`。
   - `dcap c democ` → `cd democ && make run` → 同上（C 版）。
   - `dcap ./some-template proj`（含 Makefile）→ 成功複製。
   - 缺 Makefile 的目錄 / 找不到的模板 → 應報錯（退出碼 1）。
   - 參數不足（`dcap` 或 `dcap cpp`）→ 印 usage 到 stderr、回傳 1。
3. 修正錯誤直到全綠。

## 風險

- **R2**：多檔專案 `find` 遞迴搜尋在極大樹上略慢；不影響正確性。
- ~~R3（`DCAP_HOME` 的 `cpp_libs`/`c_libs` 可能不存在）~~：已不適用——**已移除 `DCAP_HOME`**，include 就是 `-I.`。
- **R4**：`DCAP_TEMPLATES` 未設時，非 c/cpp 的裸名無來源 → 明確報錯 `template not found`（預期行為）。
- ~~R5（Windows `find` 需為 Git 附帶版）~~：已不適用——POSIX-only，不再處理 Windows。

## 已確認決策

1. 名稱：**dcap** ✅
2. 定位：**POSIX-only**（以 Linux 為主），已放棄跨平台 ✅
3. 唯一指令：`dcap <template> <name>`（無子指令、無 help）✅
4. 模板三種來源：內建 c/cpp（`#embed`）、具名外部（`DCAP_TEMPLATES`）、路徑式 ✅
5. 合法模板 = 目錄底下有 `Makefile` 或 `makefile` ✅
6. 產生的 Makefile 用 `find` 無限遞迴、輸出 `bin/<name>`（無 `.exe`）、無 `-static` ✅
7. 不使用 CMake，本體用 Makefile + g++ ✅
8. **已移除 `DCAP_HOME`**：include 就是 `-I.`（C 模板仍連結 `-lm -lpthread`）✅
9. 單檔 ≤150 行，多模組 ✅
10. 安裝：自行加 PATH，不做系統層安裝、無安裝腳本 ✅
