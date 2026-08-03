# INDEX — dcap 專案地圖

整個專案的頂層導航。dcap = **輕量、跨平台（Windows/MinGW + UNIX）的 CLI 自動化建置工具，用於隨手快速建立與管理純 C 或 C++ 的腳本式小專案；提供 `new` / `new-c` / `build` 三個指令**。AGENTS.md 只放主工作流 + 指向本檔；細節從這裡分流出去。

---

## Repo 佈局

> 本工作流採非侵入式導入：kernel 全部收在專案根的 `wf/`（本檔就在其中），專案根只留 `AGENTS.md`、`CLAUDE.md` 兩個入口。下表路徑相對專案根。

| 路徑 | 內容 |
|------|------|
| `src/`（或原始碼目錄）| dcap CLI 原始碼：C++20、`std::filesystem` / `std::system`，多模組、每檔 ≤150 行；產出單一執行檔 `dcap`（Windows 為 `dcap.exe`）|
| `Makefile` | 以 `mingw32-make`（Windows；UNIX 為 `make`）建置的建置腳本 |
| `wf/workflows/` | 開發工作流（入口見 [WORKFLOWS.md](WORKFLOWS.md)）|
| `.claude/commands/` | slash 指令（如 [`/wf-tick`](../.claude/commands/wf-tick.md) 驅動定期心跳；位於專案根 `.claude/`）|
| `wf/inbox/` | agent 之間的**信件**收件匣（放信處，保持乾淨；使用方式見 [workflows/inbox/](workflows/inbox/README.md)。可選）|
| [wf/plan/](plan/README.md) | dcap **設計計畫**（拆成多檔，每檔 ≤150 行）：環境/跨平台、模組拆分、指令規格、部署/驗證/風險 |
| `docs/` | 使用者文件（含 `docs/html/`）|

## 工作流

工作流的**選擇與入口**見 **[WORKFLOWS.md](WORKFLOWS.md)** 的派發表——它由你導入時選的 **flavor 包**（開發 / 知識工作）提供並貼入。每個工作流的 durable 知識歸在 `workflows/<該工作流>/` 或單檔 `workflows/<該工作流>.md`（含 `archive/` 封存過時文檔），具體流程在各自入口檔。

[DEV-GUIDE](DEV-GUIDE.md) 是**被動的結構整理參考**（結構整理原則 + 四級成長軌跡）——**只在要重構/整理結構時取用**。always-on 的**鐵律**在 [AGENTS.md](../AGENTS.md)；碰原始碼的**程式碼慣例 + 導航 index 維護鏈**在 `common/conventions`（由開發 flavor 包提供）。

## 通用（跨工作流共享）

| 路徑 | 內容 |
|------|------|
| [common/README](workflows/common/README.md) | 跨工作流共通：[gotchas](workflows/common/gotchas.md) 踩坑（kernel 內建）+ `conventions` 程式碼慣例（開發 flavor 提供）+ `writing` 寫作風格（知識 flavor 提供）|

## 活狀態（只列還沒完成的）

三軸：進度＝我手上的、待使用者＝卡在人、信件＝agent 之間收發（像 email）。

| 檔案 | 用途 |
|------|------|
| [SESSION-LOG](SESSION-LOG.md) | 進度 hub（repo 根）→ 各工作流 session-log（open-only）|
| [WAIT_USER](WAIT_USER.md) | 待**使用者**親自做/驗證的入口（repo 根；膨脹後拆 `wait_todo/` 分類檔）|
| `inbox/`（放信處）+ [workflows/inbox/](workflows/inbox/README.md)（使用方式）| agent 之間的**信件**（可選；像 email，狀態靠位置：inbox 頂層＝未處理、`done/`＝已處理）|
