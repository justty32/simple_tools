# P1a-2 process seam 工作規格

> **待驗目標、非正式 ABI。** phase 0先完成 B1/B2；process phase只把 `first` fake換成 P0 process，`second`保持 fake。authority總則見 [`06`](06-FUNCTION-TASK-MODEL.md)。

## 1. 固定 fixture

```text
sequence-two root Task
  first  -> process leaf -> attempt-1 -> one P0 invocation
  second -> existing fake leaf
```

parent success oracle只有 `exited(0)`／expected stdout／empty stderr。nonzero、signaled、spawn_error仍是完整已知 Returns，可建 leaf Receipt；composite聚合延後。P0 `outcome_unknown`才是 unknown。

## 2. Call、stdin、environment與 generation

P1a-2 prototype Call明示：

```text
definition:{absolute_path,generation_sha256}
argv:[...]
stdin:{encoding:"base64",data:"..."}
cwd:absolute_path
environment_policy:{kind:"inherit"}
```

base64須 strict/canonical decode並與 `stdin.bin`逐 byte相等；這只是 prototype subset。

內部入口 `accept_fixture_root(root_id, resolver)`才可 resolve root；純 `recover_store()`不 resolve，pre-planned crash後須顯式 re-accept才可換 staging。child由 accepted parent stable slot re-stage；兩者不可混用。

resolver把 executable解為 absolute regular-file path，以內容 SHA-256作 generation並寫 Call。Task只在寫 `dispatch_intent{attempt_id}` **之前**驗 current hash；mismatch即 `repair_required(generation)`、0 intent/dispatch。Task intent後 Definition再變不能推翻完整舊 evidence；若 evidence缺失仍 repair且不重跑。

只支持 `inherit`。P0 Request未存 actual env，binder不驗環境，也不宣稱隔離/snapshot/reproducibility。success fixture環境無關；另以 probe證明 ambient variable確實繼承。非-inherit在 Task intent前 repair，dispatch `0`。

## 3. 執行與 read-only binder

layout：

```text
tasks/<first>/
  events.jsonl                         # dispatch_intent唯一 Task authority
  attempts/attempt-1/p0/<uuid>/        # P0 raw artifacts
  payload/<hash>.receipt.json          # commit前只是 staging
```

沒有 attempt-local intent file或額外 evidence materialization。P0 UUID只是 raw location；P0 intent不能回推 Task intent。

runner只重用 P0 public `FunctionStore.run`/Request，不複製、不走 CLI。binder是自有 strict零寫入 reader：不 construct `FunctionStore`，不用 inspect/private validator/recover。

每件 authority artifact先 `lstat` regular/non-symlink，再 bounded read。JSON拒絕 duplicate keys/NaN/trailing data並驗 exact fields/type/version。

Task attempt組合規則：

- `dispatch_intent`＋0 P0 UUID：repair/unknown，無 Receipt、不重跑。
- 無 Task intent卻有 UUID：corruption；不得由 directory或 P0 intent補 Task event。
- 同 attempt >1 UUID：corruption；不得挑最新者。
- Task intent＋唯一 UUID：strict bind完整 evidence或 repair。

## 4. Request binding 與 evidence hash

binder比對 P0 `request.json`的 executable/argv/cwd與 frozen Call，並把 Call base64 stdin解碼後與 `stdin.bin`逐 byte比對。environment不在 P0 Request，僅驗 Call policy是 `inherit`。

完整 evidence manifest精確列七個檔案各自的 `{sha256,size}`：

```text
request.json
stdin.bin
request-ready.json
dispatch-intent.json       # P0 invocation intent
receipt.json               # P0 termination/stream metadata
stdout.bin
stderr.bin
```

`receipt.json`的 stream hash/size須與 raw bytes一致。排除 P0 `receipt-ready.json`與 `terminal.json`；projection不影響 evidence identity。

```text
evidence_hash = sha256(
  "aos-p1a2-process-evidence/v1\0" || canonical_json(manifest)
)
```

prototype固定 manifest canonicalization；完整 raw可在 crash後重算同一 evidence hash。

## 5. Task Receipt 與 known Return

leaf Receipt canonical bytes直接綁：

```text
task_id, call_hash,
basis:{attempt_id,p0_invocation_id,evidence_hash},
return:{termination,stdout_ref,stderr_ref}
```

先發布 `payload/<receipt_hash>.receipt.json`，再 append唯一 semantic completion：

```text
receipt_committed{receipt_hash,size}
```

payload在 event前只是 staging；raw完整時可丟棄/重建成同一 Receipt。crash後只補 payload或 commit，不 respawn。commit後 Receipt/payload或七件 raw evidence tamper均 corruption。parent驗 commit後才 `child_observed`；composite Receipt的 ordered child refs保留 `slot`。

完整 `exited(nonzero)`、`signaled`、`spawn_error`都可走相同 leaf Receipt；stdout/stderr照 P0 evidence保存。只有本 fixture的 exited0/expected/empty會讓 parent繼續 second並成功。

## 6. 次序與既有 P0 failpoints

```text
accept_fixture_root:
  root stage -> root_planned(call_ref) -> task.json -> accepted
  -> root_linked(call_ref) -> eligible

recover_store:
  validate registry -> complete only planned roots -> traverse linked roots

first:
  child stage -> child_planned(call_ref) -> task.json -> accepted
  -> child_linked(call_ref) -> generation/env checks
  -> Task dispatch_intent(attempt-1) -> FunctionStore.run once
  -> bind raw -> Receipt payload -> receipt_committed -> parent observe
```

P0現有 crash hooks只有 `after_intent`、`after_receipt_json`、`after_receipt`；沒有 after-spawn或 stream hooks。partial cases用測試直接注入 artifacts。真 after-spawn case不改 P0：專用 child fixture先寫/fsync side-effect，再 `SIGKILL` parent writer；recovery看見 intent但無完整 evidence，必須 repair且不重跑。

## 7. 最小 crash／falsification matrix

| Case | Recovery oracle |
|---|---|
| root staged後、planned前 crash | `recover_store()`見0 roots並忽略 staging；只有顯式同-ID re-accept可 re-resolve/replace |
| `root_planned`後各階段 crash | recover補 task.json→accepted→root_linked；planned/linked皆比對完整 call_ref |
| root duplicate/逆序/完整壞 line | corruption；final torn tail只在全 store projection合法後 truncate/fsync |
| child staged後 definition改變 | parent stable slot可 re-stage；planned後 mismatch則 corruption |
| generation mismatch或 non-inherit env | accepted→repair，Task intent/dispatch count `0` |
| Task intent後、runner建 P0前 crash（0 UUID） | repair、無 Receipt、second不跑、永不 respawn |
| P0 `after_intent`（1 UUID、raw不完整） | repair、無 Receipt、不 respawn |
| UUID存在但無 Task intent；或 >1 UUID | corruption，不反推 intent、不挑 UUID |
| injected partial/invalid raw | repair、無 Receipt；不得把 partial升級 |
| P0 `after_receipt_json`／`after_receipt` | 七件 raw完整即可重建 Receipt並 commit；ready/terminal不是必要條件 |
| fixture fsync side-effect後殺 parent | side-effect可見但 evidence不完整：repair、不重跑 |
| raw完整，Receipt payload/commit前後 crash | 只補 deterministic payload/event；dispatch count維持1 |
| intent後 Definition改變＋完整 raw | 不重驗 current generation；照 frozen Call/evidence commit |
| known nonzero/signal/spawn_error | binder保留分類並建 leaf Receipt；parent policy不在本片定案 |
| P0 `outcome_unknown` artifact | 不算 known evidence；Task repair且無 Receipt |
| commit前 raw tamper／commit後 tamper | 前者 repair；後者 corruption；都不 respawn |

報告列 registry projection、Task events、Call/generation、attempt、UUID數、manifest/evidence/Receipt hashes與 errors。只聲稱 WSL `/tmp` process-kill/failpoint evidence，不聲稱 power loss、TOCTOU安全、environment reproducibility、common composite ABI或 C++ writer。
