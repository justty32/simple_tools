# dcap — AI agent 專案備忘

dcap = **極簡的 POSIX/UNIX（以 Linux 為主）C / C++ 專案 scaffolder：唯一指令 `dcap <template> <name>`，把模板原樣複製成 `./<name>/`、只在 `Makefile`/`makefile` 內替換 `@NAME@`、`git init`**。本檔是**最頂層路由器**：只指向下一層，**durable 細節一律不寫這裡**。

## 先讀哪裡

- **使用者要你動手做某件事** → **[WORKFLOWS.md](wf/WORKFLOWS.md)**：依使用者意圖派發到對應工作流，再讀該工作流入口。
- **想看專案長怎樣** → **[INDEX.md](wf/INDEX.md)**：repo 頂層結構地圖。

## 分層思想（本專案的組織原則）

整個 repo 是一棵**分層樹**，每一層**只指向下一層、不存下層的細節**：

```
AGENTS.md（本檔，最頂）→ WORKFLOWS.md / INDEX.md → 各工作流入口 → 工作流內容 → 子工作流…
```

- **README**＝初入一個資料夾**先讀的入口／導引**；**INDEX**＝**描述該資料夾頂層結構**的索引。小資料夾兩者合一，大了才分出獨立 INDEX。
- **durable 知識歸到它所屬的那一層／那個工作流**，絕不往上堆——所以 AGENTS.md 才這麼薄。要某主題的細節，順著上面的樹往下走，不在本檔找。
- **鐵律（always-on，任何工作流任何時候都遵守）**：
  1. 重構/整理必須**不改變原意**（行為不變、改完跑驗證：以 `make` build 並端對端跑 `dcap cpp demo && cd demo && make run`，應印出 `2 + 3 = 5`；再跑 `make test` 應印出 `all tests passed`；無 lint）。
  2. **未經確認不 push、不開新工作**（commit 到主分支是慣例，push 先確認）。
  3. 各工作流的**具體流程在它自己的入口檔**，不在頂層。
- **[DEV-GUIDE.md](wf/DEV-GUIDE.md) 是被動參考**（結構整理原則 + 四級成長軌跡）——**只在你要重構/整理結構時才取用**，不貫穿日常每個動作。只在**碰原始碼**時適用的**程式碼慣例 + 導航 index 維護鏈**在 `common/conventions`（由**開發 flavor 包**提供）。

## 開發環境

- **工具鏈**：gcc/g++（需 GCC 15+，支援 C++20 `#embed`）、git、make、sh。不使用 CMake。
- **內建模板登錄表是產生的**：`tools/gen-embed.sh` 於 build 時掃 `templates/`（每個含 Makefile 的目錄 = 一個內建模板），產出 `build/builtin_tables.inc`，`src/builtin.cpp` `#include` 它。**C++ 裡不准出現任何模板名稱**——新增內建模板 = 建目錄，別手寫 `#embed`、別在 `builtin.cpp` 或 usage 文字裡列名字。
- **定位**：**POSIX/UNIX（以 Linux 為主），已放棄跨平台**——不再有 Windows 專屬處理（`#ifdef _WIN32`、`.exe`、`mingw32-make`、`-static`、安裝腳本皆已移除）。
- **include 路徑**：產生的 Makefile 就是 `-Iinclude`（已移除 `DCAP_HOME`）。具名外部模板根目錄用 `DCAP_TEMPLATES`。
- **產生的專案是「可執行的 .so」**：單一產物 `main`，以 `-shared -fPIC` + 內嵌 `.interp` + `-Wl,-e,dcap_main` 做到既可執行又可被連結。進入點的三個約束（不能 return、無 argc/argv、必須 `force_align_arg_pointer`）見 [docs/architecture.md](docs/architecture.md)——動模板前先讀。

## 主工作流（活狀態：進度 / 待測 / 信件）

事情告一段落、因應需求結束、或臨時中止時 → 把**還沒完成**的活狀態記到進度；需要**使用者親自做／驗證**的（實機環境、外部工具實跑、需權限/本機環境）→ 記到待使用者。兩者都**只列 open**，完成即移除、不留已完成清單。

- **進度**（我自己的 open in-flight）→ [SESSION-LOG.md](wf/SESSION-LOG.md)
- **待使用者**（等使用者親自做/驗證）→ [WAIT_USER.md](wf/WAIT_USER.md)
- **信件**（agent 之間的訊息交換，像 email；放信處是 repo 根的 `inbox/`）→ 使用方式見 [workflows/inbox/](wf/workflows/inbox/README.md)（可選；單方專案不需要就整包刪）

> 三軸各管一種「還沒完的事」：進度＝我手上的、待使用者＝卡在人、信件＝agent 之間收發（寄失敗/不回都無妨）。使用者說「**看看信箱**」＝掃自己的 inbox 待辦。
