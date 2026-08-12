# 系統架構與固化飛輪

本頁畫的是延後系統的推演，不是目前 package 關係。現況與實際順序以
[`freepy/ROADMAP.md`](../../../freepy/ROADMAP.md) 為準。

## 四個平面

```text
┌─────────────────────────────────────────────────────┐
│ Workbench / CLI / agent clients                     │  認知與操作
├─────────────────────────────────────────────────────┤
│ Agent Machine                                      │  權威語意
│ image · event · goal · assurance · ledger · restart│
├─────────────────────────────────────────────────────┤
│ Evaluators and compilers                            │  機率求值
│ agentloop · context compiler · verifier · scheduler │
├─────────────────────────────────────────────────────┤
│ Trusted adapters + Linux                            │  effect/enforcement
│ endpoint · fs/db · process · container · cgroup     │
└─────────────────────────────────────────────────────┘
```

Workbench 顯示並提交 typed operation；Agent Machine 是唯一 authoritative reducer；evaluator 產生
proposal；adapter 執行 effect。檔案系統可橫跨四層作為人類可見 protocol，但 projection write
仍必須回到 kernel validator。

## 接回目前 FreePy

| 現有元件 | 在新模型中的角色 | 下一個缺口 |
|---|---|---|
| `llmkit/llms` | 一個 Step attempt 的 endpoint adapter | 保存 normalized request/raw reply 與 manifest ref |
| `agentloop` | 已有的本地 Round safe-boundary runner | 外圍 adapter 才把邊界映射成 durable work records；`advance()` 本身不產生它們 |
| `tooljson` | typed tool definition／dispatch contract | 加 effect kind、operation/receipt adapter，不破壞模型 schema |
| `base_tools` | 最小 Linux editor verbs | write precondition、delta/evidence；shell 仍需 sandbox |
| 提議中的 `agent_machine` | image、event、goal、ledger、assurance kernel | 保留研究，直到 ROADMAP 所列需求真的出現 |
| `memory_tools` | quote、object、context pager | manifest 納入 workspace/policy generation |
| `agent_runtime` | process/container、capability enforcement | actor-bound workspace view、overlay、writer lease |
| `team/communication` | 組織、delegation、causal refs | assurance dependency 與 human routing，不傳隱含 authority |
| `agentfs` | per-actor image projection | Workbench/CLI 共用 resolver；後做 control/mount |
| `introspection` | effective snapshot | 成為 Workbench read model，不另造真源 |

這張表只描述日後接法，不表示 durable service 已排入下一步。Workspace Form 不應成為搶先實作
整座 IDE 的理由；若計畫啟動，可先用 fake versioned tree 和 scripted endpoint 驗 reducer。

## 最小 logical ABI

```text
register_image(definition) -> agent_id
enqueue_round(goal, instruction, workspace_ref, operation_id) -> round_id
advance(round_id, lease) -> AskStep | RunToolBatch | Candidate | Wait
accept_step_result(lease, reply, usage, operation_id)
settle_tool(tool_run, effect_status, evidence, operation_id)
commit_workspace(intent, expected_generation, operation_id)
submit_claim(round, evidence_refs, operation_id)
verify(goal, verifier) -> Decision
control(round, pause|resume|restart|cancel, operation_id)
```

事件 envelope 統一帶 actor path、instance/incarnation、grant generation、causation id、operation id、
aggregate generation 與 schema version。這些欄位讓 UI、agentfs、replay 與 audit 看同一條真相。

## 從探索到固化

repo 已呈現一條可重複的飛輪：

```text
LLM prototype
  -> traces / failures / counterexamples
  -> versioned contract + golden fixtures
  -> Python reference reducer
  -> Janet semantic core shadow
  -> differential replay
  -> typed tool / adapter
  -> C++ native leaf（只有穩定且需要時）
  -> observed evidence 回饋下一版 contract
```

[`plan_shell.py`](../../../freepy/prototypes/README.md) 已證明探索會挖出 POSIX 假設與 TOCTOU；
[`tooljson`](../../../freepy/llmkit/tooljson/FORMAT.md) 已把探索結果變成「spec 才是產品」的跨語言
契約；[`dcap`](../../../dcap/README.md) 可為穩定 native leaf 生出最小 C/C++ project。這三者其實是
同一個 **spec crystallizer** 的不同階段。

## 語言分工

- **Python**：快速原型、provider SDK、真實 workload oracle、資料蒐集。
- **Janet／Lisp**：小型資料 schema、reducer、policy、namespace、context、condition/restart；適合
  持續檢查與重新定義穩定語意。
- **C++**：已固化的高可靠 runtime、SQLite/process/filesystem adapter 或真正熱路徑；不提前
  接管仍快速變動的 policy。
- **Linux**：process、fd、mount、cgroup、namespace、seccomp/Landlock 與最後 enforcement。

「只有 Lisp 才能應付」可改成可測假說：若核心必須把 code/state/event/restart 當同一種可檢查
資料，Lisp 的表達成本可能最低；但 Smalltalk 式 image、event-sourced actor 或強型別 reducer
也可能成立。真正必要的是反射性、版本化與可恢復求值，不是括號本身。

## 兩種 machine 互補

可以把 Agent Machine 看成兩個互鎖核心：

```text
probabilistic evaluator    產生候選、探索未知、讀取 live world
deterministic reducer      驗證、記帳、提交、撤銷、重播與限制 authority
```

Janet 或 C++ 的「固化」不是消滅 LLM，而是把已理解的行為從昂貴、易錯的 evaluator 移到便宜、
可驗的 reducer/tool。LLM 因而持續工作在尚未固化的邊界；系統越用越多的資產不是聊天紀錄，
而是 fixtures、contracts、evidence 與可替換 implementation。

## 架構原則

- world 可塑，但 kernel invariant 不可由任意 `eval` 改寫。
- path 尋址、handle 授權、schema 定義語意、event 建立真相。
- prompt、memory、workspace data 與 authority 分層，不因內容像命令就升權。
- image 可重建、diff、fork；不保存 socket、lock、PID 等任意 heap 假象。
- cache、UI、mount 都可替換；correctness 只依賴 event、store、verifier 與 enforcement evidence。
