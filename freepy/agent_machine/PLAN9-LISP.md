# Plan 9 與 Lisp 能直接用上的地方

## Plan 9：組合世界，而不是建全域物件圖

Plan 9 最值得採用的是 per-process namespace 與小型 file servers。每個 agent session 由 supervisor
組出自己的世界：

```text
/self       自己的 process image、Round、goal 與 events
/parent     經裁切的主管 view
/children   可管理的 child handles
/tools      被授予的 syscall-like services
/memory     可 dereference 的 objects/views
/work       workspace projection
/srv        可 attach 的 endpoint、verifier、artifact services
```

同一路徑對不同 actor 可以解析成不同 view。canonical agent path 是 identity；session namespace
是 environment；open handle/fid 綁 actor、instance 與 grant generation，才是 live capability。

### 可借的 protocol shape

```text
<object>/
  status        當前小型 projection
  events        cursor-based append-only stream
  data          主要內容／結果
  ctl           typed command envelope
  .meta/        schema、generation、effective limits
```

核心 provider 先定 `attach/walk/stat/list/open/read/write/flush/clunk`，Python tools、FUSE、9P 只是
adapter。`flush` 對應取消尚未完成的 request；`clunk` 釋放 handle/lease，不刪 object。

Plan 9 的 `rfork` 讓 child 對 namespace、environment、fd table、memory、notes 分別選擇 share／copy／
new，正適合把 `spawn(context="fresh"|"fork")` 改成明確 inheritance manifest：

```text
identity=new
namespace=derived
context=fresh | snapshot-ref
memory=selected refs
capabilities=subset
budget=reserved slice
events=new stream
```

不要只提供兩種巨型模板後暗中複製所有東西。

### Plan 9 自己教我們不要做的事

- `/proc` 是 process 的 view，不是 process 本體；agentfs 同樣只能 projection。
- process creation 太複雜，不應靠任意文字寫檔描述；spawn 保持 typed transaction。
- path 統一 naming/access，不代表所有資源都適合普通檔案語意。
- 9P 的固定 verbs 很適合 transport；domain schema 仍需 version/type，不能只靠文字慣例。
- namespace 減少暴露面但不自動授權；server 每次 operation 仍驗 capability。

## Lisp：讓 machine state 可求值、可檢查、可恢復

Lisp 的價值不是把所有 JSON 改成 S-expression，而是建立統一 object model：Goal、Round、Step、
Lease、Condition、Restart、Evidence 與 Event 都是可列出、打印、序列化、引用與轉換的資料。
machine step 是對 immutable state + event 的 reduction；執行器語言日後可換 Python、Janet 或
其他實作，而不改 logical schema。

### Environment 與 dynamic binding

Agent 的 effective view 類似 evaluation environment。task-scoped policy、budget lease、current
goal、trace context 可作 dynamic bindings，生命期隨 Round／operation 自動解除；global definition
與當次 effective binding 分開。

但 ACL 不可只靠 dynamic variable。binding 是方便傳遞 context；authoritative operation 仍使用
不可偽造 handle 並向 supervisor 重驗 generation。

### Condition 與 restart

Common Lisp condition system 把「偵測到情況」和「選哪種恢復方式」分開，特別適合機率 engine：

```text
Condition                       可用 Restarts
endpoint-overloaded             wait, switch-engine, lower-quality, abort
context-over-limit              evict, summarize, switch-engine, ask-user
invalid-proposal                repair, resample, switch-engine, reject
verification-failed             continue-work, fork-critic, escalate, exhaust
tool-outcome-unknown            reconcile, compensate, await-human
resource-pressure               throttle, reclaim, migrate, cancel-victim
```

condition 保存發生位置、evidence、resource owner 與 resumable boundary；restart 是當下 policy
允許的 typed continuation。agent 可以建議 restart，只有 supervisor／operator 能 invoke 會改權威
狀態的 restart。這比散落的 exception + retry loop 更能避免錯誤恢復越權或重做副作用。

### Persistent image 與 time travel

歷史 Lisp machine 的 world image 啟發 checkpoint，但 Agent Machine 不保存任意 interpreter heap。
保存的是可攜 logical image：event head、schemas、context/memory refs、pending intents、ledger 與
adapter versions。如此才能重建、diff、fork，也不把 socket、thread lock 或 provider client
誤當可持久狀態。

## 合併後的設計規則

```text
Plan 9 path       找得到 object/service
Plan 9 namespace 決定 agent 最小可見世界
fid/handle        表示已解析的 capability
Lisp object       表示可檢查的 machine data
Lisp reduction    event -> new immutable projection
condition         描述偏離正常路徑但不擅自決策
restart           在當前 dynamic scope 內恢復
Linux primitive   對實體 process/resource 做最終 enforcement
```

慢 Step 讓每一層檢查都負擔得起。仍需避免把 token 級熱路徑做成 filesystem 操作，或讓任意
`eval`／自然語言 `ctl` 繞過 schema。可塑性存在於 representation 與 composition，不存在於取消
kernel invariants。
