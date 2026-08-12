# Agent Machine M0（parked prototype）

這是 2026-08-12 提前開始、後來因分層重新定義而暫停的純資料骨架。它只有 records、commands、
events、reducer 與 in-memory store，沒有 command validation、resource ledger、scheduler、CAS 或完整
測試；不可當成正式 package。

若未來真的出現 durable control-plane 需求，應先依
[`docs/freepy/architecture.md`](../../../docs/freepy/architecture.md) 重新決定可序列化資料與 runtime
adapter 邊界，再判斷哪些程式值得沿用。
