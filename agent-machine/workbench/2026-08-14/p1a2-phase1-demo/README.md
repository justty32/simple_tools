# P1a-2 Phase 1：三分鐘試用

這個試用證明：把一個符合固定 schema 的 root Call `accept` 到**絕對路徑** store 後，重開一個 `recover` 入口所得的 JSON 報告完全相同。它也讓人直接看到 root 已是 `linked`，以及固定的 Call 雜湊。

它**不證明** child process 曾執行、完整 AOS lifecycle、power-loss durability、多 writer、NFS、exactly-once 或外部副作用回滾。Phase 1 只涵蓋 root registry／accept／recover；demo 也不展示故障注入細節。

## 執行

資料只會暫存於 WSL 的 `/tmp`。請使用有 Python 的 `Ubuntu` distro，不要依賴 Windows 預設 WSL distro。

若已進入 Ubuntu 的 WSL shell，於 repository 根目錄執行：

```sh
PYTHONDONTWRITEBYTECODE=1 python3 agent-machine/workbench/2026-08-14/p1a2-phase1-demo/demo.py
```

若從 PowerShell 執行：

```powershell
wsl.exe -d Ubuntu -e sh -lc 'cd /mnt/c/code/mine/simple_tools && PYTHONDONTWRITEBYTECODE=1 python3 agent-machine/workbench/2026-08-14/p1a2-phase1-demo/demo.py'
```

成功時會印出兩行相同的 JSON，接著印出 `PASS`。程式以 `finally` 清除自己的 `/tmp/aos-p1a2-phase1-demo-*` 目錄；不會在 repository 建立 store 或 `__pycache__`。

## 使用感受（2026-08-14 實跑）

輸入只有一條命令；輸出只有 accept、recover 與明確 PASS。Call 由 demo 產生，保留原型的精確語意：第一格為絕對 definition/cwd 的 process recipe，第二格為固定 fake 結果；本階段不會啟動它們。
