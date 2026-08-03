# dcap 規劃 (plan)

← [INDEX](../INDEX.md)｜[AGENTS.md](../../AGENTS.md)

dcap 的設計計畫，拆成多個檔（每檔 ≤150 行）。實作進行中的活狀態在 [SESSION-LOG](../SESSION-LOG.md)，此處只放**設計決策**（durable）。

| 檔 | 內容 |
|----|------|
| [01-environment-crossplatform.md](01-environment-crossplatform.md) | 環境現況 + 設計取捨（POSIX-only、include `-I.`、`DCAP_TEMPLATES`） |
| [02-structure-modules.md](02-structure-modules.md) | dcap 本體結構、模組拆分（單檔 ≤150 行）、本體 Makefile |
| [03-commands.md](03-commands.md) | 唯一指令 `dcap <template> <name>` 規格 + 模板三種來源 |
| [04-deploy-verify-risks.md](04-deploy-verify-risks.md) | 部署／安裝、驗證步驟、風險與已確認決策 |

## 一句話

dcap = 極簡的 **POSIX/UNIX**（以 Linux 為主）C / C++ 專案 scaffolder：把模板原樣複製成 `./<name>/`、只在 `Makefile`/`makefile` 內替換 `@NAME@`、`git init`。唯一指令 `dcap <template> <name>`。技術棧：C++20（`std::filesystem`、`#embed` 內嵌內建模板）、`std::system` 發子命令、每檔 ≤150 行多模組、以 `make` 建置（不使用 CMake）。**已放棄跨平台**。
