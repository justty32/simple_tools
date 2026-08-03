# simple_tools — 工作筆記

跨 session 的決定與待辦。**只記還沒完的事和不看 code 看不出來的決定**；已經寫在各專案 README 裡的東西不重複抄。

## dcap 家族

- [`dcap/`](dcap/README.md) — 原版。Makefile + `tools/gen-embed.sh` 掃 `templates/` 產生內建模板登錄表。**已完成，不再動。**
- [`dcap-2/`](dcap-2/README.md) — 重新設計版。**目前的主線。**
- `dcap-3/` — **還沒開始**，見下。

### dcap-2 的設計約束（2026-08-03 定的，都是取捨不是疏漏）

使用者當時的原話：完全捨棄 shell 腳本、內嵌檔案單檔一個個指定（不用 shell 產生）、放棄 Makefile 改用 CMake（本體與產生的專案都是）、**「完全的精簡」**、不要 clang-format、拿掉 agent 工作流。

「完全的精簡」講了兩次而且很堅決 —— 這是為什麼 dcap-2 沒有 `test/`、沒有 `fmt`／`install` target、沒有 `docs/`、沒有 `wf/`，以及四個 scaffolder 模組被併成一個 `main.cpp`。**不要好心加回來。**

反過來說，這幾樣是**刻意留下**的，別當成沒精簡到：

| 留下的 | 為什麼 |
|---|---|
| 單一產物既可執行又可連結 | 使用者被問到時明確選擇保留 |
| `c` 和 `cpp` 兩個內建模板 | 同上 |
| `lib<name>.so` symlink | soname 沒有它解不開 —— **連結時和執行時都需要**，不只是 `-l` 的方便 |
| `readelf` 偵測 loader 路徑 | 讓 musl 和非 x86-64 不用手動指定；`execute_process` 不算 shell |

跨平台這件事**已經結案**：ELF-only 是刻意取捨，README 寫了「不打算修」。WSL2 可用，但不能放在 `/mnt/c`（drvfs 建不了 symlink），ARM 版 Windows 也不行（aarch64 沒有 `force_align_arg_pointer` 那條假設）。

### dcap-3：把產物拆成兩個檔（未開始）

2026-08-03 使用者提的：「其實也不一定要 .so 和 main 同屬於一個檔案」。想放棄「單一產物同時是執行檔和 .so」那個把戲，讓執行檔和 shared library 就是兩個檔。當晚沒開工。

動機：那個把戲正是把產生的專案綁死在 Linux/ELF/x86-64 的原因，也是進入點那三條彆扭規則（不能 return、沒有 argc/argv、必須 `force_align_arg_pointer`）的來源。拆開就全部消失。

開工方式：照 dcap-2 從 dcap 複製的做法，在 `simple_tools/` 底下開一個新的兄弟目錄，從 dcap-2 複製。**dcap-2 上面那些約束全部繼承**（CMake、無 shell、手寫 `#embed`、無 clang-format、無 agent 工作流、glob 整個 `src/`、產物放專案根、跨專案用檔案路徑引用）—— 這次要重新考慮的**只有「一個檔」這件事**。`.so` 到底還要不要、還是變成可選，開工前先問。

## 慣例

- **commit 直接進 `main`**，不開分支。**push 要先問過。**（原始出處是 `dcap/AGENTS.md`，但 dcap-2 刻意沒有 AGENTS.md，所以記在這裡。）
- 重構／整理**不改變原意**，改完要跑驗證。dcap-2 的驗證是：`cmake -B build && cmake --build build` 建本體 → `dcap cpp demo` → `cd demo && cmake -B build && cmake --build build && ./main` 應印出 `2 + 3 = 5`。無 lint。
