# LangGraph：最值得研究的 durability 參考

LangGraph 1.2.10 是低階 stateful graph runtime，不只是畫 DAG。它把 StateGraph 編譯成 Pregel execution，將 state、task、writes、checkpoint、interrupt 與 stream 變成一級概念。代價是核心約 8 萬 Python 行，不能為了幾個觀念整套重造。

## 執行模型

`StateGraph` 的 node 是 `State -> Partial<State>`；每個 state key 可宣告 reducer，合併同一 superstep 的多筆 update（[state.py](https://github.com/langchain-ai/langgraph/blob/d56666f7fbf0d380ad84cdf0cbe5aa48ab0cc086/libs/langgraph/langgraph/graph/state.py#L130-L144)）。

Pregel step 大致是：

1. 從 checkpoint、channel versions、pending writes 準備 tasks；
2. 同一步的 tasks 可並行與 retry；
3. 收齊 writes 後按 deterministic task path 排序；
4. reducer 更新 channels/version；
5. 寫 next checkpoint，再觸發下步。

`LastValue` 遇到同一步多寫會明確報 concurrent update error；`BinaryOperatorAggregate` 才允許 reducer 聚合。這比「大家改同一個 dict」更誠實（[apply_writes](https://github.com/langchain-ai/langgraph/blob/d56666f7fbf0d380ad84cdf0cbe5aa48ab0cc086/libs/langgraph/langgraph/pregel/_algo.py#L232-L344)）。

## Checkpoint 與 identity

`BaseCheckpointSaver` 定義 `get_tuple`、`put`、`put_writes`；`thread_id` 是 checkpoint primary key，另有 `checkpoint_ns/id`（[checkpoint contract](https://github.com/langchain-ai/langgraph/blob/d56666f7fbf0d380ad84cdf0cbe5aa48ab0cc086/libs/checkpoint/langgraph/checkpoint/base/__init__.py#L176-L319)）。saver 以 task id + write index 去重 runtime writes。

值得採用的 identity 分層：

```text
run/thread -> checkpoint version -> task(path, id, attempt) -> writes
```

但 `thread_id` 只是 lookup key，不是 tenant、ACL 或 authority；freepy 還要 agent path、instance id、actor grant。

## Interrupt / resume 的真相

`Command` 可帶 state update、goto 與 resume。`interrupt()` 首次建立 pending interrupt；resume 後 **從 node 開頭重跑**，不是從函式中間續跑（[types.py](https://github.com/langchain-ai/langgraph/blob/d56666f7fbf0d380ad84cdf0cbe5aa48ab0cc086/libs/langgraph/langgraph/types.py#L759-L932)）。

所以 node 在 interrupt 前做的 payment、email、file write 可能再次執行。LangGraph 能去重 checkpoint writes，不會自動給外部世界 exactly-once。

## 值得 freepy 採用

- state field 是單寫或 explicit reducer；未知 concurrent update 預設拒絕；
- task 完成先保存 pending writes/receipt，再合併 next state；
- saver backend 要跑同一組 idempotency conformance tests；
- pending approval/input 是可查的 durable record，resume 綁 request/task/hash；
- typed stream event 帶 task path、namespace、checkpoint，而不只 token；
- subgraph namespace 可映射 agent path/task path，但兩者 identity 仍分開。

## 不照搬

- 不引入 LangChain Runnable/callback compatibility、完整 Pregel DSL、sync/async 雙套與 remote SDK；
- reducer 不接受未審核的任意 Python callable；必須 versioned，並證明 associative/commutative，否則標成 ordered single-writer；
- checkpoint 不是 transcript、semantic memory、effect transaction 或 sandbox；
- `async` durability 有 crash window；不可逆 effect 應 sync checkpoint + outbox/receipt；
- in-memory saver 不是 production storage；
- LangSmith observability/deployment/cloud 是外部產品，不是本地 StateGraph 自動擁有的能力。

## 最小可採用切片

先做線性/DAG step barrier、SQLite append-only saver、唯一鍵 `(checkpoint, task, write_index)`、pending interrupt 與 effect outbox。node 只產生 `EffectIntent`，受信任 executor 以 idempotency key 執行並回 `EffectReceipt`。最後才考慮 nested graph 與 parallel fan-out。
