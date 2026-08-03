# WAIT_USER — 等待使用者的事

← [AGENTS.md](../AGENTS.md)｜[INDEX](INDEX.md)

需要**使用者親自做 / 驗證**才能繼續的事——例如：實機/實環境測試、外部服務登入、環境變數設定、權限操作、需要帳號的下載、**催/開/fork 另一個 agent 處理急件**（見 [inbox](workflows/inbox/README.md)：寄了信但很急、對方可能沒開）。Claude 能做結構性驗證＋打包到極限；跨不過去的那一關記這裡等使用者。

**只列還沒做的**——做完即移除（不留已完成清單，歷史看 git log）。

> **膨脹就拆**：待使用者項堆多了，就開 **`wait_todo/`** 資料夾按類別拆檔，本檔退回只留導航到各分類檔（照 [DEV-GUIDE「結構整理原則」](DEV-GUIDE.md)）。

## 待使用者項

- **讓 `dcap` 全域可用**：把 `C:\code\mine\dcap\bin` 加入 PATH，或複製 `bin\dcap.exe` 到已在 PATH 的目錄（如 `C:\dev\mingw64\bin`）。UNIX 可跑 `sudo ./install.sh`。
- **（可選）為 dcap 本體建立 git 倉庫**：目前專案根尚未 `git init`（尚未被要求）。需要版控時自行 `git init` 並首次 commit。
