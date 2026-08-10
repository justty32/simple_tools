# 慢速機率求值模型

## 為何值得重新造一層 machine

傳統 OS 的控制路徑必須為微秒級事件妥協；Agent Machine 的 Step 通常耗時數秒。因此每個
semantic transition 都可以有 immutable input、typed proposal、policy check、resource charge、
evidence 與新 snapshot。要最佳化的是「每單位成本提交多少正確、可恢復的轉移」，不是每秒
跑多少本機函式。

這使 Lisp machine 式目標成為實際工程選擇：所有重要 machine objects 都可命名、列出、
序列化、重新解釋與 fork，不把權威狀態藏在任意 Python object graph。

## 權威 objects

```text
BotRecord       persistent agent image；system、history head、namespace、policy ref
GoalRecord      specification、postconditions、evidence policy、budget、status
RoundRecord     一次使用者指令到停止的執行；phase、pending events、resource ownership、stop reason
StepAttempt     一次 endpoint ask；preset、input manifest、lease、raw response、usage、termination
ToolRun         call、capability decision、effect id、result／unknown outcome
Evidence        observation、producer、source refs、trust、scope、generation
Decision        verifier 對 claim 的 accept／reject／defer 與理由
```

snapshot 是 event journal 的 projection。object id 永不重用；stable name 可以移向新 generation，
舊 handle 不可悄悄跟著移動。

## Round 狀態機

```text
queued -> runnable -> leased -> running_step
   ^                              |
   |                              v
   +--- waiting_tool <- settling proposal
   +--- waiting_input
   +--- paused

running_step -> candidate | failed | cancelled | exhausted
candidate -> attempt_completed | runnable
```

`leased` 表示拿到有限期限的 dispatch 權，不表示 Step 已成功。`settling` 先記錄 raw response、
usage 與 tool intents，再發布下一狀態；crash recovery 才能分辨「沒送出」、「可能送出」與
「已提交」。

Goal 有自己的狀態機，verifier 不會倒過來改寫 Round 已發生的執行歷史：

```text
pending -> active -> verifying -> achieved
              ^          |
              |          +-> active | blocked | rejected
              +----------+

任意 active state -> exhausted | cancelled | failed
```

## Proposal、effect 與 commit

model message 是 proposal envelope：文字、tool intents、memory claims、completion claim 都還
沒有 authority。trusted runtime 依序做：

1. 驗 response 結構、request/Step/generation 配對。
2. 先 charge 已發生的 endpoint usage；即使 proposal 被拒也不能退款。
3. 將 tool intent 轉成帶 operation id 的 typed command。
4. 驗 capability、scope、budget 與當前 world generation。
5. append intent/event 後才 dispatch effect。
6. tool 結果完整 settlement 後，Round 才回 runnable，準備下一個 Step。

跨外部系統不能假裝有 ACID rollback。優先用 idempotency key；否則保存 intent、unknown outcome、
查詢／人工 reconciliation 與 compensation。未知副作用絕不自動重跑。

## Goal completion

Goal 是 postcondition，不是 prompt。engine 只能提交 claim：

```text
Claim {goal_id, attempt_id, assertion, evidence_refs, unresolved}
Decision {verifier, method, accepted, reasons, evidence_generation}
```

verification 分三類：

- mechanical：測試、schema、hash、檔案或外部 API observation。
- probabilistic：critic／judge；結果仍帶 model provenance 與不確定性。
- authoritative：使用者或獲授權 supervisor 明確接受。

同模型多次 sample 的錯誤高度相關，不可把票數直接當置信度。需要 redundancy 時改變 engine、
evidence source、解法或 verifier。無法驗證時狀態是 `unverified`／`blocked`，不是假完成。

## Replay 與 fork

- replay：重放保存的 events，得到同一 projection；不重新呼叫模型或副作用工具。
- re-execute：用原 manifest 再 sample，建立新的 Step/attempt branch。
- resume：從已提交 safe boundary 繼續；未結清 tool pairing 必須先 settlement。
- fork：複製獲准的 image/context refs，產生新 identity/attempt；不複製 live lease。

至少保存 preset id 與解析後 engine、model/provider revision（若可得）、parameters、request id、
context manifest hash、raw response、usage、tool schema version 與外部狀態不可重播的聲明。seed
只是 provenance，不是 deterministic replay 保證。

## Safety 與 liveness 分開

domain kernel 可以保證：不越權、不超賣、不重複 commit、事件可恢復、stale result 不污染新
generation。它不能保證模型理解 goal，或任意 goal 在有限 budget 內成功。

retry、repair、switch-engine、shrink-context、escalate-to-human 都是明確 policy decision；不能
用一個模糊的 `error` 或模型自己的自信文字代替。
