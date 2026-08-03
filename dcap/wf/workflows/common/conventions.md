# 程式碼慣例 + 導航 index 維護鏈（碼相關工作流共用）

← [common/README](README.md)｜[INDEX](../../INDEX.md)

碰原始碼的工作流（feature-dev / refactor / specs / plans…）共用這套規矩。純文檔/調查類工作流用不到。結構整理原則（被動、按需取用）在 [DEV-GUIDE](../../DEV-GUIDE.md)；always-on 鐵律在 [AGENTS.md](../../AGENTS.md)。

## 程式碼慣例

- **語言/技術棧**：C++20，倚賴標準庫 `std::filesystem`（路徑/檔案操作）與 `std::system`（呼叫外部指令）。
- **單檔行數門檻**：每個原始碼檔 **≤150 行**（與 [DEV-GUIDE](../../DEV-GUIDE.md) 觸發 A 一致）；超過就按領域拆成新模組（**多模組**設計，一檔一職）。
- **跨平台**：原始碼保留 UNIX 慣例字串（路徑、指令），執行期用 `#ifdef _WIN32` 偵測平台差異；不引入 cmake，建置一律走 `mingw32-make`（Windows）/ `make`（UNIX）。
- **include 路徑**：產生的 Makefile 的 include 就是 `-I.`（已移除 `DCAP_HOME`）；不要在碼裡寫死絕對路徑。
- breaking change 前先全域 grep 受影響處，同一 commit 一併更新。

## 導航 index（code map）維護鏈

「code map」＝描述程式碼結構的導航 index（哪個檔負責什麼領域、測試在哪）。dcap 尚小，一個檔就夠；大了按領域拆成多份子 index（此時可獨立成 `common/code-map/` 資料夾）。目前尚無原始碼落地，等程式碼大到 agent 找檔困難時再建 code map。

三個面向構成維護鏈：**程式碼 → code map → 文檔**。

**優先級（衝突或時間不夠時，依序保持一致）：** 程式碼 > code map > 文檔。
**code map 與程式碼衝突時：以程式碼為準，立即修正 code map。**

**日常規則：**
1. **修改前**：先讀 code map，找到相關領域，只讀清單中列出的檔案——不要讀無關領域的檔案。
2. **修改後**：若新增或刪除了原始碼檔案，或某檔案的職責有顯著改變，必須同步更新 code map。
3. 原始碼檔案本身**不加**「對應 code map」的註釋（維護成本過高）；反向查找直接 grep code map 文件。
