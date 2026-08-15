# AOS 深入閱讀入口

第一次接手請先看 [START-HERE](../START-HERE.md)。本套是問題導向的濃縮導讀，來源是 [`full/` 完整設計](../full/README.md)的 00–13 與 [`full/options/`](../full/options/README.md) 的 01–09。它不是另一份規格；遇到細節或狀態差異時，以連結的完整原文、最新使用者決定及實際測試為準。

## 先選一條閱讀路線

### 10 分鐘：先建立共同語言

1. [核心模型](2026-08-14-deep-dive-01-core-model.md)：Function、Call、Task、Attempt、Return、Receipt 到底差在哪裡。
2. [已驗／未驗](2026-08-14-deep-dive-04-evidence.md)：目前真的跑過什麼，以及第一個真 process seam 為什麼仍不等於完整 Phase 2。

### 30 分鐘：理解整個安全故事

依序再讀：

3. [持久化與恢復](2026-08-14-deep-dive-02-durability-recovery.md)：為何要先寫下意圖，再跨入外部作用。
4. [排程與 Agent](2026-08-14-deep-dive-03-scheduling-agent.md)：root 寫入權、忙碌三路、父子不自鎖。
5. 九項選項地圖：[01–05](2026-08-14-options-essence-01.md)／[06–09](2026-08-14-options-essence-02.md)：哪些只是目前推薦，下一個小原型要推翻什麼。

### 要看所有細節

從 [`full/00`](../full/00-STATUS-AND-SOURCES.md) 開始依序讀到 [`full/13`](../full/13-LIMITS-AND-LATER.md)，再進九份 [`options`](../full/options/01-FUNCTION-ENTRY.md)。完整稿描述 AOS 想成為什麼；實際行為仍以程式、測試與其工作台 README 為準。

## 五種標記先記住

| 標記 | 怎麼讀 |
|---|---|
| **使用者已定** | 產品方向，後續實作不可偷偷違反。 |
| **目前推薦** | 現在認為安全、簡單的工程方向；仍可被反例推翻。 |
| **原型暫選** | 為了讓測試可重現而固定；即使全綠也不是正式 ABI。 |
| **尚未決定** | 保留多案，不能由文件作者替使用者定案。 |
| **明確延後** | 已知道問題存在，但第一版刻意不處理。 |

## 目前一句話狀態

AOS 已有 Linux process 邊界、兩個假 child 的保存順序，以及第一個真 process 接入 Receipt／composite tree 的窄幅 crash/recovery 證據。完整 Phase 2 的中斷矩陣、已知失敗與 golden bytes 仍未補齊；Agent、正式排程器、斷電與多寫入者也都沒有驗證。

狀態原文：[`full/00`](../full/00-STATUS-AND-SOURCES.md)、[`full/12`](../full/12-IMPLEMENTATION-AND-EVIDENCE.md)、[2026-08-14 工作台](../workbench/2026-08-14/README.md)。
