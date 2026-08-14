# Opus 外部審查收件匣

這個資料夾只用來交給 Claude／Opus 做外部審查。請先讀本輪對應的
`REQUEST*.md`。

## 何時處理

- 只有對應 Request 寫 `Status: READY` 時才開始審查。
- `Status: WAIT` 表示尚未備妥；請不要讀 source、不要掃 repo、不要花 token 回覆。

## 審查範圍

- 只讀對應 Request 明列的 source paths 與問題。
- 不得修改任何 source 或本資料夾以外的檔案。
- 回覆寫到該 Request 指定的檔名。
- 每輪請保留既有 request 與 review；新一輪另建檔案，不覆寫歷史。

## 前一輪

第一輪既有審查在
[`../opus-p1a-review/OPUS-REVIEW.md`](../opus-p1a-review/OPUS-REVIEW.md)。

第二輪已收到：[`REQUEST.md`](REQUEST.md) 與
[`OPUS-REVIEW-02.md`](OPUS-REVIEW-02.md)。它只審穩定規格，未讀取正在變動的
P1a-2 Python prototype。

## 目前狀態

第三輪已收到 [`OPUS-DESIGN-03.md`](OPUS-DESIGN-03.md) 與
[`OPUS-REVIEW-03.md`](OPUS-REVIEW-03.md)。使用者補充忙碌與排隊方向後，03A 也已收到
[`OPUS-ADDENDUM-03.md`](OPUS-ADDENDUM-03.md)。目前不需要再啟動第三輪。

第四輪 [`REQUEST-04.md`](REQUEST-04.md) 仍是 `WAIT`，目前不要啟動。
