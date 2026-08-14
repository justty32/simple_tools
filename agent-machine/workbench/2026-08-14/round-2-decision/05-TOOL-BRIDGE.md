# Tool Function 的橋接

> **工作稿、非正式 AOS ABI。** 本頁只定 Agent 語境中的 terminology 與 bridge；Call/Task 儲存真源見 [`06-FUNCTION-TASK-MODEL.md`](06-FUNCTION-TASK-MODEL.md)。

## 核心 mapping

**使用者已定：**Step與Round都是Function，Tool可沿用process底座。**P1工作模型：**Tool是Function的Agent角色；binding把模型Tool Call轉成AOS Call，accepted後建立Tool Task record。

```text
model Tool Call --schema/permission/binding--> Function Call
       | invalid/denied                         --accept--> Tool Task
       |  0 Task + paired Tool Result                       |
       |                                         known Task Receipt
       `----------------------------------------------------> paired Tool Result
                                                  unknown -> repair result
```

- 每個 model Tool Call 依 model call ID 恰好配一筆 Tool Result。invalid/denied 沒有 AOS Task，仍要關閉模型配對。
- `Tool Call`、`Function Call`、`Task`、`Function Return`、`Task Receipt`、`Tool Result` 是不同物件，不可混名。
- leaf/composite 是執行形狀；Step/Tool/Round 是 Agent roles。Tool 可為 leaf 或 composite。

## Call、Task、Return、Receipt

**內部+Opus 暫選：**

- process leaf binding暫用Definition、argv、stdin、cwd、environment policy；不把此shape升成其他Function ABI。model call ID、Task ID、parent、slot與state留在Call外。
- `call_ref{hash,size}` 是 Call identity；Task-local `call.json` 只是 P1 resolver policy。相同 Tool Function Call 可因兩次接受而形成不同 Tool Tasks。
- P1 Task Receipt綁`task_id + call_ref + basis`。leaf basis指向attempt；composite basis是ordered child Receipts。
- Function Return 是 caller-facing result。P1 可把 Return payload 內嵌在 Receipt，但 Tool Result 必須由 Agent bridge 明確轉換，不能把任一層 artifact 直接改名。
- unknown 沒有 Receipt；Tool Result 要誠實表示 unknown/needs repair，不能用文字成功遮蓋。

## Composite Tool 與 relation

parent Tool ordered events 是 children relation 的唯一 authority。每個 child 自存其 Call/Task/Receipt；parent Receipt 只引用 ordered child Task IDs/Receipt hashes，不複製 child output。child backlink 或 wakeup index只能是可重建 projection。

Round 通常觀察：

```text
Round Task -> Step Task -> Tool Task -> Step Task
```

child unknown 阻擋 Tool/Round parent Receipt。Step 是否可有 AOS-visible children 留 P1b，不在 Tool bridge 偷定。

## leaf process 不是 Task Receipt

```text
Call
  -> Task / attempt
  -> P0 request + process bytes/termination（staging evidence）
  -> binder validation
  -> Function Return embedded in Task Receipt
  -> Tool Result
```

P0 名為 receipt 的檔案仍只是 process evidence。P1a-2 strict reader比對 request executable/argv/stdin/cwd、raw streams與 termination；P0未保存 actual env，只能驗 Call policy是 inherit。Task Receipt以 content-addressed payload＋`receipt_committed` event綁 Task/Call/attempt/P0 invocation/evidence。詳見 [`07`](07-P1A2-PROCESS-SEAM.md)。

## 三種 leaf adapter

### JSON machine mode

JSON stdin/stdout 是 Tool Definition 的 protocol，不是 Machine 新原語。binding 明列 schema version 與唯一 argv/stdin mapping；decode failure與 process termination 分開保存。

### Large/binary/file format

以 filesystem paths＋manifest/content-type/size/hash 傳遞，不硬塞 argv。immutable Call 不代表路徑內容已 snapshot；live/checked/snapshot 仍是 [`02-OPEN-OPTIONS.md`](02-OPEN-OPTIONS.md) 的選項。

### Server API

HTTP 由 curl-like local executable adapter 承接，仍是 leaf Tool Function。endpoint、credential policy、HTTP decode 屬 adapter；AOS Machine 不新增 HTTP instruction。

## 四層結果

1. **Process evidence**：launch/exit/signal 與 raw streams，僅 staging。
2. **Protocol Return**：stdout 是否形成有效 envelope。
3. **Transport Return**：HTTP status、timeout、DNS/TLS 等。
4. **Application/Tool Return**：成功、拒絕、部分完成或失敗。

Task Receipt 記錄「哪個 Task 基於哪些 evidence/children 得到哪個 Return」，不取代各層 Return；Tool Result 則關閉 model call pairing。common Return ABI 仍待選，P1a-2 只驗單一 deterministic success。

## Catalog 與 generation

- Agent Tool catalog 保存模型可見 schema/binding generation並指向 callable Definition path；不是 Machine central registry。
- 每次模型請求前凍結 visible generation，回來的 Tool Call 按該版驗證。
- **P1a-2 待驗：**只為first process leaf，在child plan前解析absolute executable並以content SHA-256作generation。
- dispatch 前 generation mismatch 即 `repair_required`、dispatch count `0`、無 intent；不重新 resolve。這不 snapshot dependency/tree，也不承諾 TOCTOU。

## Human 與 machine surface

同一Definition可有human CLI與machine mode。`aos exec ./ask`、`aos exec .`、是否搜尋PATH及何時解析明確target都留P1c比較。

## 明確不做

P1a-2 不做 secrets、streaming、large-output retention、full HTTP mapping、common ABI 或 C++ Task writer。後續 C++ seam只能產生 `ValidatedProcessEvidence`，不能寫 Task events或 relation。
