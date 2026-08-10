# Python demo → Janet 固化流程

## 核心原則

Python 是**探索器與 reference oracle**；Janet 是**穩定語意的第二實作**。兩者共同依賴外部契約，不互相 import 內部 object。

固化不是 code translation，而是：

```text
觀察行為 → 凍結契約 → 建第二實作 → 差分驗證 → 逐步切流量
```

## Stage 0：Python probe

目標是把未知問題撞出來。允許快速、醜、provider-specific，但必須記錄：

- 輸入、輸出、錯誤與 side effects。
- provider／OS／版本。
- 不變條件與已知未定義行為。
- 可以重播的 fixture 或 event trace。

尚未穩定的 API 不急著搬。

## Stage 1：凍結語言中立契約

將邊界改成 JSON/NDJSON、bytes、argv、exit status、typed event。移除：

- Python object identity、pickle、任意 callable。
- 由 docstring/signature 反射才知道的必要語意。
- 未驗證 raw shell/runtime argv。
- exception class 作為跨行程協定。

錯誤也要資料化，例如 `{kind, message, retryable, details}`；若維持「工具結果永遠字串」，至少另留 structured audit event。

## Stage 2：抽 pure core

先在 Python 內把 decision 與 effect 分開：

```text
decide(state, event) -> new_state + intents
execute(intent)      -> result event
```

Janet 只需重做 `decide`。檔案、網路、process、container 由 adapter 執行 typed intent，再把結果送回 core。這也方便 replay。

## Stage 3：Janet shadow implementation

Janet 讀同一批 fixtures，不碰 production side effect。每筆比較：

- normalized output／schema。
- state transition 與 stop reason。
- intent 順序、idempotency key。
- path、bytes、排序、timeout 邊界。
- error classification。

差異必須被歸類成 Python bug、Janet bug、規格空白或平台差異；不能只把 fixture 改到綠。

## Stage 4：接相同 effect adapter

先把 Janet core 包成 CLI／stdio service，沿用 Python 已驗證的 adapter。或反過來：Python host 呼叫 Janet decision CLI。這時要加：

- protocol version handshake。
- request id、deadline、cancel、output cap。
- crash isolation 與 restart。
- deterministic log／trace。

避免 Janet FFI 直穿 Python ABI。

## Stage 5：切換預設

切流量順序：offline replay → shadow → opt-in → default → 移除 fallback。每一步都有 rollback 指標：semantic mismatch、crash、latency、memory、unhandled event、平台失敗。

Python reference 至少保留一個穩定期；它是 oracle、migration aid 與 emergency fallback，不必因 Janet 上線當天立刻刪除。

## Stage 6：刪除或永久保留

若功能只是 pure core，穩定後可刪 Python duplication。以下適合永久保留 adapter：

- `_type:"python"` plugin。
- OpenAI/provider SDK。
- research/data collection。
- 尚未有可靠 Janet backend 的平台功能。

## 每個 slice 的 Definition of Done

- spec 有版本與 unknown-field policy。
- golden fixtures 同時跑 Python/Janet。
- property／fuzz 測 path、排序、額度與 parser。
- crash/retry 不重複 side effect。
- clean build 可重現。
- Linux CI 通過；Windows 若不支援要明確 fail closed。
- 文件說清楚 Janet 擁有哪個 invariant、adapter 擁有哪個 effect。

## 不適合固化的訊號

- 每週還在改 message shape。
- 測試只能連真模型才能跑。
- 行為依賴某 Python library 的隱藏副作用。
- 沒有辦法重播或比較。
- 移植只為「看起來更 Lisp」，沒有可靠性、可組合性或部署收益。
