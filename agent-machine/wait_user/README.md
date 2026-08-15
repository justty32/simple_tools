# AOS 閱讀入口

這裡有兩種閱讀深度：幾分鐘的小卡，以及人在家、有時間時看的完整濃縮包。兩者都是導讀，不取代 [`full/`](../full/README.md) 完整設計、最新使用者決定或實際測試。

第一次接手請先看 [START-HERE](../START-HERE.md)；瀏覽器入口是 [index.html](index.html)。

## 深入閱讀包

從 [AOS 深入閱讀入口](2026-08-14-deep-dive-00-index.md) 開始。它把 `full/00–13` 與 `full/options/01–09` 按問題重組成六個主題：

- [核心模型](2026-08-14-deep-dive-01-core-model.md)
- [持久化與恢復](2026-08-14-deep-dive-02-durability-recovery.md)
- [排程與 Agent](2026-08-14-deep-dive-03-scheduling-agent.md)
- [已驗／未驗](2026-08-14-deep-dive-04-evidence.md)
- 選項精華 [01–05](2026-08-14-options-essence-01.md)／[06–09](2026-08-14-options-essence-02.md)

目前最重要的進展是：第一個真 Linux process 已接到 Task Receipt 與 composite tree 的窄路徑；完整原始證據可在重開後只補提交，after-spawn（process 啟動後）結果不明也不重跑。這仍不是完整 Phase 2；精確測試數字與缺口見[已驗／未驗](2026-08-14-deep-dive-04-evidence.md)。

## 幾分鐘小卡

- [一次執行，AOS 要記住什麼？](2026-08-14-p0-function.md)
- [bot-a 還在忙，第二件事怎麼辦？](2026-08-14-agent-busy.md)（已回覆）

忙時可以完全不看、不回覆；設計會按目前標明的推薦方向繼續。回饋不需要使用正式名詞，一句直覺就可以。

## 已確定的 busy 結論

結論是：普通新呼叫直接回覆忙碌、不建立 Task；追加到目前 Task 或另建排隊 Task，都必須由使用者明確選擇。
