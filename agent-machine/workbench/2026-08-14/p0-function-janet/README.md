# P0 Function / Janet 工作台原型、非規格

這個小切片試驗 Janet 的合理邊界：它只對呼叫描述做純資料 admit、對已觀測的結果做 audit，及作簡單 memory-mode policy。接受後輸出是**與輸入脫離的 schema-aware snapshot**：argv 轉為 tuple，limits、termination、stdout、stderr 依 schema 重建；呼叫端之後 mutation 輸入，不會回頭改變舊結果。它不 spawn、不能讀檔、不會送網路，也不會執行或提交 Function。

`admit` 強制 `:executable`、`:argv` array（元素必須是 Janet string）、`:cwd`、`:stdin-size`、`:limits`、`:memory-mode` 全部存在且型別/範圍正確；沒有猜測預設值，也不會重寫 argv 的值。空 argv 元素、空白、`$()`、`;` 都只是合法資料；NUL 一律拒絕。

`audit-receipt` 將完整程序 receipt 與 `:outcome-unknown` 分開：前者必須帶 `:termination`（`exited` / `signaled` / `launch-error` 各自必要欄位）及 stdout/stderr 的 byte size、64 個小寫 hex SHA-256；後者不是程序 receipt，只有 reason，不可夾帶 termination。輸出一律帶 `:type`、`:decision`；這是實作上的 detached snapshot，不是 Janet 的語言級 immutable 保證。

`choose-memory-mode` 只比較 caller 要求與「有並行 writer」這個 boolean；它不讀 filesystem。`live` 保持明示；有 writer 時回 `snapshot`，否則回 `checked`。

未來 C++ embedding 可用 [sample-output.json](sample-output.json) 作跨實作差分資料：JSON 的 snake_case 對應 Janet keyword 的 hyphen（例如 `stdin_size` ↔ `:stdin-size`）。C++ 仍必須在 OS 邊界**複製、重新驗證並負責 commit**；此原型不提供授權或執行保證。

實測（Windows，Janet `1.41.2-local`）：

```powershell
cd C:\code\mine\simple_tools\agent-machine\workbench\2026-08-14\p0-function-janet
janet .\p0_function_policy.janet
# p0_function_policy: 25 tests passed
```

感受：Janet 很適合這種很小、無副作用、可直接拿結構做測試的 guard/policy 層；tagged struct 也方便 C++ 對照。不適合在此層承擔 Windows/Linux process semantics、handle/pipe、檔案快照或 commit：那些仍應留在 C++。Linux gate 未跑（目前 WSL 沒有 Janet），因此不能據此宣稱已驗證 Linux mechanics。
