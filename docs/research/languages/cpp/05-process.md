# Python demo → C++ 固化流程

## 核心原則

Python 是探索器與 reference oracle；C++ 是穩定契約的 production implementation。遷移不是逐行翻譯：

```text
觀察 → 凍結 wire/trace → 抽 reducer → C++ shadow → 差分/模糊測試 → 分階段切換
```

## Stage 0：量測與凍結範圍

每個 slice 先記錄：輸入、normalized output、錯誤分類、side effects、OS/provider/version、延遲與記憶體 baseline。沒有 workload benchmark 前，不以「C++ 更快」當 acceptance criterion。

規格仍在探索就留 Python。特別是 provider quirks、模型 tool reliability、Round control 新語意與尚未實作的多 agent plans。

## Stage 1：語言中立契約

邊界只用：

- versioned JSON／NDJSON event。
- bytes + explicit encoding/content type。
- argv array、env map、exit/signal、stdout/stderr records。
- typed error `{kind, message, retryable, details}`。
- stable ids、deadline、idempotency key。

移除 Python object identity、pickle、exception class、任意 callable 與「靠 import side effect 才註冊」。Python reflection 可以產 schema，但 runtime contract 必須落成 JSON。

## Stage 2：先在 Python 抽 reducer

```text
decide(state, event) -> state + intents
execute(intent)      -> result event
```

這一步會先揭露規格空白：例如 `Handle.say()` 與 completion 的 race、tool timeout 後 partial output、重試是否重跑 effect。若 Python 都無法以 trace 說清楚，C++ 只會把模糊語意編譯得更硬。

## Stage 3：C++ value core

要求：

- RAII ownership，避免共享裸 pointer。
- variant/state enum exhaustiveness。
- expected/result 型錯誤；exception 不跨 ABI／thread entry。
- no hidden global registry in core。
- pure functions 可直接 fuzz，不需 HTTP／filesystem。

C++ 只讀 fixtures，先不執行 production effect。對每筆比較 normalized JSON、transition、intent 順序、error kind、idempotency key 與 limits。

## Stage 4：接入邊界

優先順序：

1. JSON CLI／stdio sidecar：最容易 diff、crash isolate 與 rollback。
2. C ABI：需要 in-process latency／embedding 時才加。
3. native C++ host：等 core 與 adapter 都有獨立證據再成為主行程。

不要一開始用 pybind 把任意 Python object graph 直接暴露給 C++。那會把 GIL、reference count、exception、interpreter shutdown 與 ABI 全綁在一起，反而失去語言中立契約。

## Stage 5：native adapters

逐個 adapter 接入，每個有自己的 fault injection：

- process：spawn failure、timeout、signal、pipe fill、child tree、partial output。
- storage：crash before/after rename/commit、duplicate operation。
- HTTP/SSE：split frame、malformed JSON、disconnect、retry、cancel。
- runtime：reserve/start/collect/cleanup 各階段失敗。

core 不因 adapter 換語言而改 schema。高權限 supervisor 最好獨立 process，讓 C++ crash 不拖垮 UI／provider host。

## Stage 6：切流量

```text
offline replay → shadow → opt-in → default → 移除 fallback
```

每階段觀察 semantic mismatch、crash、latency、RSS、unhandled event、平台失敗與 recovery。Python reference 至少保留一個穩定 release；`_type:"python"`、research、provider SDK 可以永久保留。

## 每個 slice 的 Definition of Done

- spec version、unknown-field policy、canonical output 明確。
- golden/property/fuzz 同時驗 Python/C++。
- Linux clean build + ASan/UBSan；並發 slice 另跑 TSan 或等價 race 檢查。
- parser/path/protocol 有 fuzz corpus，crash 可最小化重播。
- retry 不重複 side effect；deadline/cancel 有 trace。
- C ABI 不暴露 STL、exception、allocator ownership。
- dependency version、license、artifact hash 可追。
- benchmark 對準真 workload；若只省微秒但引入數千行 adapter，停止遷移。

## 不適合固化的訊號

- 只是為單一 binary 美感，沒有部署或可靠性目標。
- 每週改 schema，fixture 一直跟著實作重寫。
- 只有 happy-path smoke，沒有錯誤／crash／race semantics。
- 需要任意 Python import/callable 才能相容。
- C++ adapter 的 dependency、TLS、cancel 行為比 Python SDK 更不透明。
- 團隊沒有長期維護 native build、debug symbol、sanitizer 與 CVE 的意願。
