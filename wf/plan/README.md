# dcap 規劃 (plan)

← [INDEX](../INDEX.md)｜[AGENTS.md](../../AGENTS.md)

dcap 的設計計畫，拆成多個檔（每檔 ≤150 行）。實作進行中的活狀態在 [SESSION-LOG](../SESSION-LOG.md)，此處只放**設計決策**（durable）。

| 檔 | 內容 |
|----|------|
| [01-environment-crossplatform.md](01-environment-crossplatform.md) | 本機環境現況 + 跨平台取捨（含 `DCAP_HOME` 環境變數） |
| [02-structure-modules.md](02-structure-modules.md) | dcap 本體結構、模組拆分（單檔 ≤150 行）、本體 Makefile |
| [03-commands.md](03-commands.md) | 三個 CLI 指令規格（`new` / `new-c` / `build`） |
| [04-deploy-verify-risks.md](04-deploy-verify-risks.md) | 部署／安裝、驗證步驟、風險與已確認決策 |

## 一句話

dcap = 輕量、跨平台（Windows/MinGW + UNIX）的 CLI 自動化建置工具，用來隨手快速建立與管理純 C 或 C++ 的腳本式小專案。技術棧：C++20（`std::filesystem`）、`std::system` 發子命令、每檔 ≤150 行多模組、以 `make`/`mingw32-make` 建置（不使用 CMake）。
