# LightAgent 與 CrewAI

兩者都把 tools、memory、guardrails、multi-agent、workflow 與 observability 放進同一套 Python framework。最有價值的是它們已碰到的工程邊界；最危險的也是核心變得很重。

## LightAgent：借 policy/trace，不借大單體

v0.9.6 的 `LightAgent` 同時持有 provider、tools、memory、hooks、guardrails 與 mutable run state；主要 `core.py` 約 2.8k 行。功能確有實作，但 README 的名稱不一定等於嚴格語意。

### 真正值得參考

- `HookContext`／`HookDecision` 將 before-run、before-tool、memory、handoff 等 phase 收成一致 middleware（[hooks.py](https://github.com/wanxingai/LightAgent/blob/2ea8917d75902c03d2a95af6fad2ba5aaa409ae2/LightAgent/hooks.py#L27-L69)）。
- Human approval 把 action/arguments 與決策綁定；重跑時不相符就 fail closed（[review.py](https://github.com/wanxingai/LightAgent/blob/2ea8917d75902c03d2a95af6fad2ba5aaa409ae2/LightAgent/review.py#L181-L298)）。
- Trace 有 parent/run-group 關係與 JSONL export，適合轉成小型 append-only event schema（[tracing.py](https://github.com/wanxingai/LightAgent/blob/2ea8917d75902c03d2a95af6fad2ba5aaa409ae2/LightAgent/tracing.py#L18-L117)）。
- memory metadata 包含 scope、source、trust、expiry；內部 reflection 可先成 candidate，明確 promotion 後才寫入。
- eval case 可對 output、tool、event、latency、recovery、cost 作 regression assertion。

### 不照搬

- mutable agent instance 在 run 前重設 trace/usage/candidates，不適合同一 instance concurrent runs；
- `LightSwarm` 主要是 registry/wrapper，handoff 依模型文字與 substring，不是可靠 scheduler；
- 所謂 ToT 實際是多次 prompt chain，不是搜尋樹；runtime tool filtering 還有未完成路徑；
- regex guardrail、in-memory shared pool、JSON checkpoint 都不能當 security/durability boundary；
- tool schema 只做基本型別檢查，外部 effect 沒有 OS sandbox。

## CrewAI：把自治與 deterministic flow 分開

CrewAI 的重要洞見是有兩個 runtime：**Crew** 管角色型 agent collaboration；**Flow** 管 event-driven control。`Task` 是大型 Pydantic contract，包含 description、expected output、agent、context、tools、structured output、async、callback（[task.py](https://github.com/crewAIInc/crewAI/blob/17f107c197e64ea486a1c985a36b3c4aecb20d28/lib/crewai/src/crewai/task.py#L120-L220)）。

Crew `kickoff` 依 sequential/hierarchical process 跑 task，最後 drain memory writes、收 usage、清理 event scope（[crew.py](https://github.com/crewAIInc/crewAI/blob/17f107c197e64ea486a1c985a36b3c4aecb20d28/lib/crewai/src/crewai/crew.py#L988-L1079)）。hierarchical mode 會建立 manager agent 與 delegation tools；它仍是 agent runtime，不是權限系統。

Flow 的 `start/listen/router` 把方法連到 event；state 可為 Pydantic/dict/JSON schema。persistence 有抽象 backend 與 SQLite，kickoff 可按 state id restore；human feedback 也能保存 pending state 後 resume（[Flow runtime](https://github.com/crewAIInc/crewAI/blob/17f107c197e64ea486a1c985a36b3c4aecb20d28/lib/crewai/src/crewai/flow/runtime/__init__.py#L1982-L2246)）。

### 值得參考

- task 要有 expected output/output schema，不只自然語言角色；
- autonomous Crew 與 deterministic Flow 分層，不把每一步都交給模型；
- listener/router、state restore、pending human feedback 是好用的 workflow primitives；
- typed lifecycle event bus 可接 audit/UI；memory/knowledge 是明確的 prompt-build extension point。

### 不照搬

- 核心約 508 個 Python files，`crew.py`、agent core、task 都是千行級；不符合 freepy 小核心方向；
- `execute_async` 使用 daemon thread/Future，不是 structured concurrency；
- tool/delegation 沒有統一 capability/transaction/sandbox boundary；
- memory/knowledge 最終多以文字拼進 prompt，必須補 provenance 與 trust block；
- guardrail retry/resume 仍可能重做外部 effect；
- README 的 AMP control plane 是商業層，不能算 OSS core 已有的 governance/security/deployment；
- telemetry 與分享設定要預設 local/off，再逐欄審查 payload。

## 最小採用切片

採 LightAgent 的 immutable policy decision/approval receipt/trace schema，加 CrewAI 的 typed Task、event router 與 pending human input。不要採兩者的 agent 大物件、任意 tool surface、prompt-driven delegation 或 remote telemetry defaults。
