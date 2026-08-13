# 計算模型與核心資料

## 名詞只保留六個

| 名稱 | 工程上的意思 |
|---|---|
| Function | 可呼叫的程式定義；外部名稱是 `bot-a`，狀態在 `.bot-a/` |
| Round | Function 的一次呼叫；從收到指令到等待、完成或失敗 |
| Instruction | 交給某類 executor 的一次工作，例如 LLM call、read、write、tool run |
| Step | 現有 FreePy 用語：一次 `ask() -> message`；等同一條 LLM instruction |
| Operation | client 要求核心改狀態的命令，帶唯一 id 與預期 root generation |
| Commit | 核心驗證後發布的新 VFS generation |

「LLM endpoint 是 CPU」保留為有用的抽象：scheduler 能把一條 instruction 派給 LLM、shell、
檔案或其他 executor。但 LLM 不像硬體 CPU 那樣可靠；它的輸出只能先成為候選結果。

## 最小資料結構

Python 先用 frozen dataclass；C++ 固化為 value type。以下欄位是邏輯格式，不指定記憶體布局：

```text
FunctionRecord {
  function_id, node_id, active_version, config_ref, state_ref,
  refs_ref, policy_ref, revision
}

RoundRecord {
  round_id, function_id, instruction_ref, phase, context_head,
  budget, stop_reason, revision
}

Instruction {
  instruction_id, round_id, kind, executor_ref, input_manifest,
  parameters, base_generation, operation_id, deadline
}

InstructionResult {
  instruction_id, lease_id, status, output_refs, proposed_changes,
  usage, external_receipt, error
}

CommitRecord {
  commit_id, parent_root_generation, root_generation, operation_ids,
  mutations, refs_delta, journal_head, git_oid?
}
```

識別碼永不重用。人看的 path 可以 rename；`node_id` 不變。v1 只有一個全 namespace 的
`root_generation`，所以 commit 是單 writer；operation 的 `expected_root_generation` 不符合就拒絕。
record 的 `revision` 只是該物件內容版本，不是另一套 CAS。日後確有吞吐瓶頸才另立 ADR 決定分區，
不能先混用 per-node 與 global generation。

Operation 是尚未接受的 idempotency envelope；Instruction 是核心接受後產生的 immutable work record。
只有 proposed mutation/commit 對 `base_generation` 做比較，executor 不得自行改預期版本。

`input_manifest` 是 executor 本次真正看見的有序輸入，至少記錄：

- system prompt、message 與 tool schema 的 ref、hash、generation 和排列順序；
- model、endpoint、parameters、provider revision（取得到才記）；
- 明確載入的檔案／resource ref；
- policy、budget、權限與 tool version；
- request id，以及哪些外部狀態無法重播。

這讓 replay 與 re-execute 分得開：replay 只重放已保存的 commit；re-execute 會建立新的
instruction id，再呼叫一次 executor，不能假裝得到原本結果。

## Round 狀態

Round 只描述函數呼叫的生命週期，不混入 worker thread 或 OS PID：

```text
created -> queued -> ready -> running -> ready
                     |         |
                     |         +-> waiting_for_executor
                     +-> waiting | paused | verifying

waiting / paused / verifying / waiting_for_executor -> ready
非 terminal phase -> completed | failed | cancelled | exhausted
```

`running` 表示有 active instruction，不表示某條 OS thread 永久屬於 Round。合法 transition 會以 closed
enum/table 實作；不存在的狀態或跳轉一律拒絕。

每條 Instruction 另有自己的狀態：

```text
prepared -> leased -> dispatched -> result_received -> committed
                         |                  |
                         v                  v
                 outcome_unknown      rejected / failed
```

這樣可以精確回答「crash 發生時，工具到底送出去沒有」。`outcome_unknown` 不得普通 retry；先查詢
外部系統、執行補償操作，或交給使用者處理。

以下四件事也必須分開：

1. endpoint 已回覆；
2. LLM 沒再要求工具，Round 暫時安靜；
3. Round 已經 terminal，不能 resume；
4. 使用者目標經檢查後真的完成。

LLM 說「完成」只會讓 Round 進 `verifying`。檔案 hash、測試結果、schema、外部 API observation
或使用者確認等 verifier 通過後，才可進 `completed`。不能驗證時應是 `waiting` 或 `failed`，
不能用文字自信度填補。

## 一條 instruction 的提交流程

```text
1. 讀取 root generation G，編出 immutable input_manifest。
2. 驗權限、預算與 capacity，將 prepared record 寫入 durable journal 並 fsync。
3. worker 取得有期限的 lease，才可 dispatch LLM 或 tool。
4. completion/receipt/unknown 與 usage 先寫入 durable journal，再 charge/settle。
5. 驗證 result、operation id、lease 與 base generation G 是否仍有效。
6. 將允許的修改寫入 staging generation G+1。
7. 原子發布 G+1；之後才讓 scheduler 派下一條相依 instruction。
```

Worker 只能回 `InstructionResult`。它不能持有可直接修改權威狀態的 Python object、C++ pointer
或 Janet table。

## 核心不變量

- 一個 operation id 最多提交一次；重送要回原結果或明確 duplicate。
- 同一 root generation 只有一個成功後繼；競爭者以 stale generation 失敗。
- 已發生的 endpoint/tool 成本不因提案被拒絕而消失；未使用的 reservation 只退一次。
- active exclusive writer 同時最多一個；lease 過期後的晚到結果不能寫入新 generation。
- assistant tool calls 提交後，每個 call id 都必須有已提交結果（包含失敗結果），才能再問模型。
- Commit 完成後，正式效果必須能從 VFS 觀察；外部效果至少要看到 intent、receipt 或 unknown。
- cache miss 只能影響效能，不能改變正確性或權限。
- 未知 schema version、event kind、instruction kind 與 policy output 一律拒絕。

## 與現有 FreePy 的接法

`llmkit.LLM/Bot` 是第一個 LLM compatibility adapter；`tooljson`、`base_tools`、`exec_tools`、
`http_tools` 提供第一批工具能力。現有 Step 在 Reply 提交後、tool dispatch 前有 `after_step` safe
boundary；tool batch 是下一條獨立 instruction，不屬於 Step。

它們都不是持久化真源。每次 worker 從 input manifest 重建短命的 Bot/Handle clone，只回 immutable
raw Reply 或 tool receipt；核心 commit 後才 materialize canonical message/call/result records。現有
`Bot.ask()` 會直接改 history，因此 live Bot/Handle 絕不能跨 instruction 當 authority。callback、
dispatch callable、thread 也不進 records；第一個 LLM adapter可直接用 `LLM.think` 加純 Reply parser，
`agentloop.advance()` 只作之後的相容橋接。
