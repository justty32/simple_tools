# Agent framework 參考調查

日期：2026-08-10

## 結論先講

這七個專案都值得看，但**沒有一個適合整套搬進 freepy**。最好的做法是保留 freepy 現有的小核心、Turn／Round、logical agent path、typed operation、event log 與 agentfs projection，再抽取七種可獨立驗證的設計切片：

1. **Instructor**：schema normalization、validation re-ask、provider handler registry。
2. **LightAgent**：一致的 policy hook、approval receipt、trace event、memory promotion。
3. **LangGraph**：checkpoint contract、pending writes、interrupt/resume 與 reducer。
4. **PocketFlow**：`prep → exec → post` lifecycle、action edge、flow-as-node。
5. **Swarm**：handoff 是顯式工具結果，caller 持有全部狀態。
6. **CrewAI**：Task 的 expected output、agent/task/flow 分層與 event-driven flow。
7. **MiniChain**：lazy operation graph、backend protocol 與 execution snapshot。

優先順序不是「功能最多者勝」。Instructor 不是 agent orchestrator，PocketFlow 的核心只有 100 行，LangGraph 則是完整 durable graph runtime；它們解的是不同層。

## 對 freepy 最重要的判斷

- 不換掉 `agentloop`。它已經有更清楚的 **Turn／Round** 定義；外部框架反而常混用 turn、step、retry。
- 不讓 workflow framework 成為權限邊界。工具仍要經 typed intent、effective grant、approval、sandbox 與 receipt。
- 不把 checkpoint 當 memory，也不把 agent path 當 authority。checkpoint 保存執行游標；memory 保存可引用內容；path 只是 identity/namespace。
- 不以 decorator 或 mutable dict 作為 durable ABI。核心狀態應有 schema、版本、instance id、operation id 與 append-only evidence。
- 先實作跨框架共同指出的窄切片：**可審計的模型／工具執行閘門**。

## 建議的第一個切片

```text
model response
  -> schema normalize / validate
  -> validation failure: append attempt + re-ask (新 Round)
  -> ToolCallIntent
  -> schema + grant + policy hooks
  -> optional approval receipt
  -> sandboxed executor
  -> ToolReceipt + trace event
  -> checkpoint / next Round
```

這同時吸收 Instructor、LightAgent、LangGraph 的強項，而且能直接接現有 `tooljson`、`agentloop` 與規劃中的 runtime/agentfs。

## 導覽

- [方法、快照與限制](01-method.md)
- [七個框架的定位比較](02-landscape.md)
- [Swarm 與 Instructor](03-swarm-instructor.md)
- [MiniChain 與 PocketFlow](04-minimal-frameworks.md)
- [LightAgent 與 CrewAI](05-integrated-frameworks.md)
- [LangGraph](06-langgraph.md)
- [映射到 freepy／agentfs](07-freepy-fit.md)
- [採用順序與驗收](08-adoption.md)
- [風險、反例與非目標](09-risks.md)
- [HTML 導覽](index.html)

## 一句話原理

這些框架本質上都在把「模型可能回什麼」收斂成可執行的控制流：有的靠 schema、有的靠 handoff、有的靠 graph。freepy 應再多做一步，把每次控制流變化寫成可追蹤事件，並把真正副作用放在受權限與隔離保護的 executor 後面。
