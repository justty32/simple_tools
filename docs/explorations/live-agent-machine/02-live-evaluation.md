# 持續求值與版本語意

## 一個 Step 固定，一個 Round 可變

現有時間定義很適合承接 live image：Step 是一次不可任意切半的 `ask() → message`；Round 是
經過多個 Step、tool 與介入的活動。可把 machine transition 寫成：

```text
M_s = compile(image@g, instruction queue, policy, memory, workspace view)
P_s ~ engine(M_s)
D_s = validate(P_s, capabilities, budget, world generation)
image@g+1 = commit(image@g, D_s, evidence)
```

`M_s` 送出後不可暗中混入新檔案或指令，否則無法回答模型當時看到什麼。人在模型推理期間
存檔、追加 instruction 或加入 tool 都合法，但先形成 pending event；完成當前 Step／tool batch
後，在下一個安全邊界編譯新的 manifest。這正是現有
[`ROUNDS.md`](../../../freepy/agentloop/ROUNDS.md) 的語意，不需假裝能中斷遠端推理。

## Workspace edit 是第一級事件

目前工具結果主要是給模型看的字串；新的模型還需要 kernel 看得懂的 effect record：

```text
WorkspaceIntent {
  operation_id, actor, round, base_generation,
  scope, operations, expected_hashes
}

WorkspaceSettlement {
  status: committed | conflict | denied | failed | unknown,
  before_generation, after_generation?, changed_paths, evidence_refs
}
```

流程是 intent 先落 event/outbox，Linux adapter 才 materialize 寫入，之後觀測實際 hash 並
settle。外部系統無法做 ACID rollback 時，`unknown` 必須保留；不能因 timeout 就盲目再寫。
這延伸了 [proposal／effect／commit](../../freepy/future/agent-machine/MODEL.md)，也讓「修改函數」成為
可 replay、diff、fork 和稽核的 machine transition。

## Live redefinition 的規則

1. **Step pin generation。** context manifest 同時保存 workspace root/hash、policy generation、
   tool schema version 與 ordered blocks。
2. **修改在 boundary commit。** 已送出的 Step 繼續使用舊 view；新 definition 從下一 Step 生效。
3. **寫入帶 precondition。** `expected_hash` 或 `base_generation` 不符時產生 conflict condition，
   不做 last-writer-wins。
4. **多檔先進 overlay。** build/test 可在 staging view 執行，最後 guarded commit；不能保證的
   外部 effect 仍獨立 settlement。
5. **goal 另外驗。** 寫入成功只證明 world 變了；測試、schema、hash 或人類 acceptance 才能
   支撐 postcondition。

可提供的 restart 包括 `re-read`、`rebase`、`fork`、`discard`、`ask-human` 與 `abort`。agent 可以
建議，只有 policy/supervisor 能讓會改權威狀態的 restart 生效。

## 並行不是共享一個可變函數體

同一 history branch 仍應只有一個 active Round writer。大量 agent 若直接共用 rw workspace，
會使模型依舊 view 規劃、在新 view 寫入，最後留下無法歸因的狀態。較安全的形狀是：

```text
              immutable workspace@g
                /       |       \
        overlay A   overlay B   human edits
             |           |          |
        evidence A   evidence B   delta H
                \       /          /
                 commit coordinator
                         |
                  workspace@g+1
```

Git 可 materialize branch／merge，但「哪一支符合 goal」仍由 verifier 和 policy 決定。patch 能乾淨
套用不代表原本意圖仍成立；自動 rebase 也必須留下 condition/evidence。

## Replay、resume、fork 不可混稱

- **replay**：只重放已保存 events，重建同一 logical image；不呼叫模型或重做副作用。
- **re-execute**：固定舊 manifest，再取一個新 stochastic sample；建立新 attempt branch。
- **resume**：從已提交 safe boundary 繼續；pending tool pairing 或 unknown effect 先 reconcile。
- **fork**：複製獲准的 image/workspace refs，建立新 identity/branch；不複製 live lease。

這也解釋 dormant agent：沉睡的是 evaluator，不是刪掉 function。logical image、event head、
workspace refs、goal 與 memory 仍在；喚醒時重新驗 policy、generation 與 pending effects，不能把
舊 process heap 當成永遠有效的世界。

## 尚不深入的問題：history feedback

agent loop 會把自己先前的 message、tool call 與結果納入後續 Step，所以 prior result feedback
是運算路徑的一部分，而不只是事後 debugger data。普通 Lisp evaluator 不會自動把每次輸出
回送給下一個 form；這可能更接近 recurrence、stateful transducer、reflective evaluator 或帶
event history 的 fixed-point process。

目前只保留這個差異，不替它命名或推導架構。要判斷它是否真是「自省」，仍須分清：讀自己
的輸出、讀外部 event log、修改自身 definition，以及對 evaluator 本身反射，是否為同一件事。

## 最低不變量

- 已送出的 Step manifest 永遠可重建。
- stale generation、stale activation、撤銷 capability 均不可 commit。
- edit、rename、mkdir、delete 都是 binding/domain 變更，須同等稽核。
- crash 後 effect unknown 不自動 retry。
- source trace、workspace history 與失敗嘗試不因 goal achieved 被洗掉。
- completion claim、Round stop 與 Goal achieved 永遠是不同事件。
