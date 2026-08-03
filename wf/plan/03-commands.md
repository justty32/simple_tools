# 03 — CLI 指令規格

← [plan 索引](README.md)

## 唯一指令：`dcap <template> <name>`

- `argv[1]` = 模板；`argv[2]` = 要建立的新專案目錄名。
- 行為：把解析到的模板目錄底下的東西全部原樣（逐位元組）複製成 `./<name>/`，複製時跳過 `.git`，只在新專案的 `Makefile`/`makefile` 內把 `@NAME@` 替換成 `<name>`（其他檔案內容與所有檔名都不變），最後對新專案執行 `git init`。
- 參數不足（`argc < 3`）→ 印 usage 到 **stderr** 並回傳 1。
- **沒有子指令、沒有 help 指令**。已移除的舊指令：`new`、`new-c`、`build`、`help` 全部不存在了。

## 模板三種來源（`argv[1]` 的解析）

| 形式 | 判定 | 解析 |
|------|------|------|
| 內建 | `c` / `cpp` | 以 `#embed` 內嵌內容，永遠可用，不需環境變數，不查檔案系統 |
| 路徑式 | 開頭是 `.` 或 `/`（`./x`、`../x`、`/abs/y`）| 相對呼叫 dcap 的 cwd 或絕對路徑 |
| 具名外部 | 裸名（不以 `.` 或 `/` 開頭）且非 c/cpp | `$DCAP_TEMPLATES/<名>`；未設 `DCAP_TEMPLATES` 則無此來源 |

注意像 `a/b` 這種「含 `/` 但不以 `.` 或 `/` 開頭」的**不是**路徑式，會被當裸名。合法模板的判定 = 該目錄底下有 `Makefile` 或 `makefile`。找不到目錄或缺 Makefile/makefile → 報錯（退出碼 1）。

## 內建模板產出

- `cpp`：`Makefile`（`g++ -std=c++20 -I.`、搜尋 `.cpp`、輸出 `bin/<name>`）、`main.cpp`、`.gitignore`（`bin/`、`build/`、`*.o`）。
- `c`：`Makefile`（`gcc -std=c11 -I.`、連結 `-lm -lpthread`、搜尋 `.c`、輸出 `bin/<name>`）、`main.c`、`.gitignore`。
- 兩者最後都 `git init`。

## 建置 / 執行產生的專案

dcap **不再**包裝建置。直接用產生的 Makefile：`make` / `make run`（`./bin/<name>`）/ `make clean`。

## 產生的子專案 Makefile — 檔案搜尋

採用 `find` 無限遞迴（已確認）：

```make
SRC := $(shell find . -name '*.cpp')   # C 版為 '*.c'
```

用 `ifeq ($(origin CXX),default)` 強制 g++/gcc，但允許 `make CXX=...` 覆蓋。輸出 `bin/<name>`（無 `.exe`），無 `-static`。
