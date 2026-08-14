# AOS 本日想法簿

> 2026-08-14 的工作記憶，不是正式規格，也不取代根層、`full/` 或 `archived/` 文件。

這裡只做一件事：把「使用者已經親自決定的事」和「仍待決定的事」、「agent 暫時推導的設計」分開，避免原型或討論久了之後，把我們自己的想法誤寫成使用者要求。

## 閱讀順序

1. [`USER-DECISIONS.md`](USER-DECISIONS.md)：使用者明確說過或接受的方向；優先度最高。
2. [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md)：使用者尚未裁決、仍可有多套方案的地方。
3. [`AGENT-INFERENCES.md`](AGENT-INFERENCES.md)：agent、Opus 與原型目前採用的解讀；可被推翻。

今日執行方式見 [`../PLAN.md`](../PLAN.md)。目前設計包見 [`../round-2-decision/README.md`](../round-2-decision/README.md)，但其中的 prototype schema 不會自動變成使用者決定。

## 判讀規則

- 使用者後來說得更完整時，以後來的內容覆蓋早期簡寫；被覆蓋處會明寫。
- 使用者只說「偏好」時，不把它寫成不可更改的硬規則。
- 程式跑通只能證明那個窄案例可行，不能反過來改寫產品方向。
- 新想法先記到本目錄；正式化、原型化或移入 archive 由主 agent 排程。
- 此處刻意使用白話繁中；必要英文只保留 AOS、Function、Call、Task、Step、Round、Tool 等正在比較的名稱。

## 本簿不保存什麼

- 不保存正式 JSON 格式、事件名稱或檔案布局。
- 不把 FreePy 現行作法當成 AOS 的現在；它只可提供歷史經驗。
- 不替使用者提前決定 `.aos`、跨機器同步、Step 邊界或 Runtime 實作。
- 不記錄每一次測試輸出；證據留在各 prototype 目錄。
