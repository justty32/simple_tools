# C++ hybrid project (executable + library)

由 dcap 產生。同時提供可執行檔與可被引用的函式庫。

## 佈局
- `include/` — 公開標頭（他人 `-Iinclude` 引用）
- `src/lib*.cpp` — 函式庫實作 → `lib/lib<name>.a` + `lib/lib<name>.so`
- `src/main.cpp` — 可執行檔進入點 → `./main`（靜態連結自家 lib，可獨立執行）
- `test/` — 測試

## 常用指令
| 指令 | 作用 |
|------|------|
| `make` | 建 `main` + `lib/` 下的 .a/.so |
| `make run` | 執行 `./main` |
| `make test` | 建置並執行測試 |
| `make debug` | `-g -O0` 重建 |
| `make install` | 裝到 `PREFIX`（預設 `/usr/local`）：`bin/<name>`、`lib/`、`include/<name>/` |
| `make fmt` | clang-format 格式化 |
| `make clean` | 清除產物 |

> `make install PREFIX=~/.local` 可改安裝前綴。
> LSP 用的 `compile_commands.json` 可用 `bear -- make` 產生。
