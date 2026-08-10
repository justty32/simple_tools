# MiniChain 與 PocketFlow

這兩個專案最適合拿來理解「最少要幾個抽象」。MiniChain 偏 lazy LLM/dataflow；PocketFlow 偏 explicit workflow graph。

## MiniChain：先描述，再求值

`@prompt` 呼叫時不立即跑模型，而是建立 `Chain(History(...))`；`Chain.run_gen` 遞迴求值相依 chain、cache 結果，再展開目前節點（[base.py](https://github.com/srush/MiniChain/blob/637d310ccd77dd7cb3197c826d0a304cafce65b2/minichain/base.py#L24-L74)）。`Model.stream` 做 template render、backend call、stream aggregation，並記 `RunLog/PromptSnap`（[base.py](https://github.com/srush/MiniChain/blob/637d310ccd77dd7cb3197c826d0a304cafce65b2/minichain/base.py#L125-L192)）。

### 值得參考

- 描述 operation graph 與執行分離，執行前可 inspect/policy-check；
- backend protocol 很小，model、transform、tool 可有一致 executor 形狀；
- 每次 prompt/backend call 保存 snapshot，適合作 trace/replay evidence；
- decorator 只作 ergonomic frontend，核心仍是資料結構。

### 不照搬

- global mutable `MinichainContext` 不適合 concurrent Turn 或 multi-tenant；
- Python backend 用 `exec`、Bash backend 用 `shell=True`，不是安全工具邊界；
- 依賴停在 OpenAI 0.28 時代，HEAD 日期為 2023-12；
- 無 repository tests，mutable defaults、空輸出與重入語意都不夠可靠。

採用時只留 `OperationSpec + dependency refs + Executor protocol + RunSnapshot`，不要引入舊 backend。

## PocketFlow：100 行的 graph kernel

`BaseNode` 保存 params 與 `action -> successor`；`Node` 固定跑 `prep → exec → post`，並在 exec 做 retry/fallback。`Flow` 從 start node 開始，依 post 回傳 action 選下一節；Flow 自己也是 Node，所以能巢狀組合（[核心 100 行](https://github.com/The-Pocket/PocketFlow/blob/f74d023f93607b8c3268133339a5e532a949898c/pocketflow/__init__.py#L1-L100)）。

本次 repository unittest 實跑 56/56 通過，覆蓋線性/條件/cycle、composition、retry/fallback、sync/async batch。

### 值得參考

- `prep` 適合解析/驗證 intent，`exec` 只做 effect，`post` 寫 receipt 並選 edge；
- action-labelled edge 比散落的 `if` 更容易畫圖、測試與審計；
- flow-as-node 足以形成 subflow，不必先造龐大 workflow DSL；
- retry/fallback 是控制流的一部分，不藏在工具內。

### 不照搬

- `shared` 是任意 mutable dict，沒有 schema、transaction、ACL 或 persistence；
- node 用 shallow copy，內部 mutable state 可能跨 run 污染；
- retry 捕捉所有 Exception，沒有 error classification、timeout、backoff、cancel；
- async parallel batch 對同一 shared state 無 isolation；
- missing successor 只 warning 後結束，對高風險流程應是明確 error terminal。

## 給 freepy 的組合

PocketFlow 適合作「可讀的 workflow AST」，MiniChain 提醒我們 operation 可以 lazy materialize；但 runtime 要換成：

```text
immutable inputs + typed RunContext
  -> prepare/validate
  -> policy-approved executor
  -> append-only receipt
  -> explicit action/terminal
```

先支援 sequential、branch、bounded retry、subflow。等有 generation-bound snapshot、transaction/idempotency 與 cancellation，再開 parallel fan-out。
