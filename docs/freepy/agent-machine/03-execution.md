# Executor、排程與模型生命週期

## Executor 的共同介面

LLM、read/write、process、HTTP、Git 與 Janet policy 都實作同一個窄介面：

```text
describe() -> ExecutorCapabilities
prepare(Instruction, Snapshot) -> PreparedWork
execute(PreparedWork, Cancellation) -> InstructionResult
reconcile(Instruction, receipt?) -> InstructionResult
```

`prepare` 不做外部副作用；核心可在 dispatch 前再次檢查輸入 hash、權限和預算。`execute` 不拿
MachineStore 的可變 reference。`reconcile` 用來處理 timeout/crash 後「可能已執行」的工作。

第一批種類：

- `vfs.read`／`vfs.write`：確定性高，先用它驗證完整提交流程。
- `llm.call`：包裝 `llmkit.LLM`，保存 raw response、usage、finish reason 與錯誤。
- `tool.invoke`：保存 `tool_spec_ref + hash + executor_kind + arguments`。v1 只允許 frozen exec spec；
  HTTP 要明確啟用，Python callable 要 named/versioned adapter 與 process boundary，否則拒絕。
- `git.checkpoint`：只處理指定 worktree/ref，不接受模型提供任意 repo path。
- `model.lifecycle`：load、healthcheck、unload 與 residency observation。

## Scheduler 保存什麼

Function 沒工作時只是一組 records，不保留 parked thread。Scheduler 維護 durable queue；worker
取得有期限的 lease 才執行一條 instruction。Round state 與 OS process/PID 是不同生命週期。

排程循環：

```text
ready queue
  -> admission（輸入、權限、預算都齊）
  -> reserve（先扣最大可能成本）
  -> lease executor slot
  -> dispatch
  -> journal completion/receipt/unknown
  -> charge actual usage
  -> settle once
  -> commit result 或進 reconciliation
```

資源分五種，不能塞成一個 `limit`：

| 類型 | 例子 | 控制方式 |
|---|---|---|
| 會消耗完 | token、金額、Step 次數 | reserve / charge / refund once |
| 同時容量 | endpoint slot、tool worker、RAM | lease / hard ceiling |
| 單位時間 | RPM、TPM、API QPS | token bucket / backoff |
| 獨占 | workspace writer、device、secret route | owner handle / revoke |
| 可丟 cache | compiled context、model residency | affinity / evict |

parent 分給 children 的 reservation 加上自身已用量不能超過有效上限。Linux cgroup 管真正的
CPU/RAM/PID；Agent Machine 的 token/endpoint 帳本是另一層，兩者只用同一 operation id 關聯。

## 第一版排程政策

1. foreground、interactive、background 三個 priority class。
2. 同級按 owner/team 做 weighted round-robin；等待時間提供 aging，避免飢餓。
3. 有等價 deterministic tool 時優先於 LLM；不能用 LLM 自述 confidence 當可靠度。
4. deadline 影響排序，但不能突破 hard budget 或永遠壓住其他 owner。
5. 429/5xx 在 endpoint pool 做共享 backoff + jitter，不讓每個 Round 各自重試形成尖峰。
6. cancel 阻止尚未 dispatch 的新工作；已 dispatch 的工作只能請求合作式取消，結果仍要 journal／
   reconcile，terminal transition 再決定能否 commit，已花成本照記。

使用者體驗是正式輸入：記錄首個 progress event 延遲、最後可見更新、queue age、總時間、取消回應
與 deadline miss。前景工作長時間沒有可見進度時，scheduler 應先發狀態，不靠增加模型取樣掩蓋。

## Context 不等於全部檔案

VFS 是可尋址記憶體，LLM context 是一次 call 的有限 working set。Context compiler 產生有序
`input_manifest`：current instruction、system policy、未閉合 tool pairing 永遠 pinned；其他檔案按
明確 load、依賴、近期使用與 token budget 選入。沒放進 manifest 的內容，模型本次就沒看見。

壓縮只建立新的 summary/object，不改寫 source trace。Load 要限制 depth、bytes、token 與權限；
同一路徑反覆載入要有 hysteresis，避免 context thrashing。

## 模型切換與 unload

模型是 endpoint pool 共享的有容量 executor，不是隨手改一個字串。切換只在 Round `ready/waiting/
paused`、沒有 in-flight instruction、沒有 pending tool calls（所有 call id 都已有結果）且 Function
不持有 writer lease 時開始：

```text
1. 將 Function 標成 switching，停止派新的 LLM instruction。
2. 等目前 lease 結清；超時則記 unknown，不假裝已停。
3. 對 `ModelResidencyKey(provider,endpoint,model)` 原子取得 lifecycle-exclusive lock，狀態由
   `active -> draining`；draining 阻止新 admission（排隊或改選別顆）。
4. 等 active lease refcount=0 且未 pin，再進 `unloading`；檢查與 unload 之間不能釋放 lock。
5. provider 宣告 `supports_unload` 才呼叫；Ollama 用 keep_alive=0，再查 `/api/ps` 確認。
6. 不支援／失敗回 typed result，狀態回 active 或 failed；不能假裝已卸載。
7. load/healthcheck 新模型，驗證 tools/images/context 能力。
8. 原子提交新的 executor ref 與 config revision，釋放 lifecycle lock，恢復 ready。
```

若新模型 load 失敗，Function 進 `waiting_for_executor`；policy 可以重載舊模型或選第三顆，但每一步
都留下紀錄。預設不讓兩顆大型本機模型短暫重疊。Round 保留 history 前，還要檢查新模型支援既有
message/tool 格式；不相容就要求 context conversion 或新 Round。

開發與 live smoke 使用 `RunBudget{max_output_tokens_per_step=4096, max_steps=12, max_calls=8,
timeout_s=900}` 作預設 profile，呼叫者可明確覆寫。每條 LLM manifest 記錄 effective parameters 及
來源（Function default、run override、model preset）；adapter 建 request-local Params，不修改共享 LLM。
Live cleanup 放在 `finally`，測完 unload 最後模型。

## 失敗分類

| 類別 | 行為 |
|---|---|
| validation | 不 dispatch；修正輸入或拒絕 |
| capacity/rate | 留在 queue，依 deadline/backoff 重排 |
| transport before-send | 可用同 operation id 重試 |
| outcome unknown | reconcile／compensate／人工，不普通 retry |
| stale lease/generation | 保存結果作稽核，不 commit |
| verifier failed | Round 回 ready 繼續做，或依 policy 結束 |
| policy/Janet crash | 使用安全預設或暫停；絕不繞過核心規則 |

Retry、換模型、縮 context、改用工具、詢問使用者和 abort 都是具名選項。模型可以建議，scheduler
只能選當前 policy 允許的項目。
