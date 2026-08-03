# 功能開發（feature-dev）— 工作流入口

← [INDEX](../../INDEX.md)｜[AGENTS.md](../../AGENTS.md)

新增 / 修改 dcap 功能的工作流（例：`new` / `new-c` / `build` 三個指令的行為，或跨平台分支）。這是本工作流的**入口**：先讀本檔，再往下深入。always-on 鐵律見 [AGENTS.md](../../AGENTS.md)；要整理結構時參考 [DEV-GUIDE](../../DEV-GUIDE.md)（被動）；**程式碼慣例 + 導航 index 維護鏈**見 [common/conventions](../common/conventions.md)。

本資料夾是「資料夾型工作流」：`landed/`、`gotchas.md`、`session-log.md` 都是**長出來才建**（見 DEV-GUIDE 四級成長軌跡），不要預先建空檔。

## 流程

```
修改（增量；守 ≤150 行/檔、多模組、#ifdef _WIN32 跨平台慣例）
  → 跑自動驗證：`mingw32-make` build + 端對端 `dcap new demo && cd demo && dcap build`（應印出 Hello World）綠燈
  → 交使用者驗證（跨平台另一側需實機）→ 回報問題 → 修 → 重複
  → 全數通過後：補齊 code map → 補文檔 → commit
```

驗證指令細節見 [testing](../testing.md)。

- **自動驗證是你（Claude）自己跑**的把關（鐵律：改完跑驗證）。
- **Claude 跑不了的驗證一律由使用者做**——先靠自動驗證＋結構性檢查把握到極限再交付；需使用者驗證的記到 [WAIT_USER](../../WAIT_USER.md)。
- 測試迭代期間，code map / 文檔可暫時落後；**commit 前必須對齊**。
- 跨 session 時在本工作流 `session-log.md` 補一行 `[功能名] 文檔/code map 待同步`，下個 session 不會誤判已同步。

## 內容

| 檔案 | 內容 |
|------|------|
| `landed/`（長出來才建）| 已落地功能目錄（時間序；功能在哪、實作細節指標）|
| `gotchas.md`（長出來才建）| 本工作流專屬踩坑 |
| `session-log.md`（長出來才建）| 本工作流 open / in-flight 進度（hub 在 repo 根 [SESSION-LOG](../../SESSION-LOG.md)）|

> **archive**：過時/被取代的文檔封存進 `feature-dev/archive/`（保留歷史、不污染現役）。本入口檔若膨脹，照 [DEV-GUIDE「結構整理原則」](../../DEV-GUIDE.md) 拆。
