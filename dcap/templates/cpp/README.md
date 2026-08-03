# C++ project (executable **and** shared library — one file)

由 dcap 產生。`make` 只產出**一個檔案** `main`，它同時是：

- 可以直接執行的程式（`./main`）
- 可以被別的程式連結／`dlopen` 的 shared library

作法：把它以 `-shared -fPIC` 連結成 .so，再內嵌一個 `.interp` 段指向系統的 dynamic loader（PT_INTERP，Makefile 會自動偵測路徑），並用 `-Wl,-e,dcap_main` 指定進入點。`lib/libdemo.so` 只是指向 `../main` 的 symlink，讓 `-Llib -l<name>` 能用。

## 佈局
- `include/` — 公開標頭（他人 `-Iinclude` 引用）
- `src/*.cpp` — 全部編進同一個產物，**沒有命名規則**（`src/main.cpp` 只是放進入點的地方）
- `test/` — 測試，連結產物本身

## 常用指令
| 指令 | 作用 |
|------|------|
| `make` | 建 `main`（+ `lib/lib<name>.so` symlink） |
| `make run` | 執行 `./main` |
| `make test` | 建置並執行測試（連結 `lib/lib<name>.so`） |
| `make debug` | `-g -O0` 重建 |
| `make install` | 產物裝到 `$(PREFIX)/lib/lib<name>.so`，`$(PREFIX)/bin/<name>` 是指向它的 symlink |
| `make fmt` | clang-format 格式化 |
| `make clean` | 清除產物 |

## 寫進入點要注意

`dcap_main` 不是普通的 `main()`：

- **不能 return** — 沒有 crt0 接手，必須自己呼叫 `exit()`。
- **拿不到 argc/argv** — 需要的話讀 `/proc/self/cmdline`。
- **必須有 `force_align_arg_pointer`** — ELF entry point 進來時 RSP 是 16-byte 對齊，但函式 prologue 預期的是「call 之後」的 `RSP % 16 == 8`；差這 8 bytes 會讓任何對齊的 SSE spill 直接 segfault（例如 `std::cout << some_int`）。

其他函式照常寫，不受影響。

> `make install PREFIX=~/.local` 可改安裝前綴。
> LSP 用的 `compile_commands.json` 可用 `bear -- make` 產生。
> loader 偵測失敗時：`make INTERP=/lib64/ld-linux-x86-64.so.2`。
