# Opus 審查請求 02

**Status: RECEIVED**

已收到回覆：[`OPUS-REVIEW-02.md`](OPUS-REVIEW-02.md)。以下保留本輪原始範圍與問題。

請只審以下已穩定的規格，不讀其他 repo 檔案：

- `agent-machine/workbench/2026-08-14/round-2-decision/README.md`
- `agent-machine/workbench/2026-08-14/round-2-decision/01-DECISIONS.md`
- `agent-machine/workbench/2026-08-14/round-2-decision/06-FUNCTION-TASK-MODEL.md`
- `agent-machine/workbench/2026-08-14/round-2-decision/07-P1A2-PROCESS-SEAM.md`
- `agent-machine/workbench/2026-08-14/round-2-decision/08-P1A2-FORMATS.md`

可選讀前輪
`agent-machine/workbench/2026-08-14/opus-p1a-review/OPUS-REVIEW.md`，
只用來避免重複提出已處理事項。

請勿讀取或評論 `p1a2-process-python`：它正在變動，不是本輪證據。
不得修改 sources；回覆只寫到本資料夾的 `OPUS-REVIEW-02.md`。

## 問題

1. Function／Call／Task 的分層，是否與 AOS 的主軸自洽？
2. root acceptance、recovery 與 child recipe，是否真有單一權威，且不會重執行？
3. process evidence 到 Receipt 的路徑，以及 known／unknown／corruption 的分界，是否合理？
4. 不擴大 P1a-2 範圍下，是否還有真正的 blocker？

## 回覆方式

繁中、先寫結論。只列 blocking 或高把握度問題；若沒有，直接說明未見 blocker。
不要提出跨機器、CAS、正式 ABI 或其他未來擴張。保留此 request，勿覆寫歷史。
