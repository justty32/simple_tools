# P1a-2 exact prototype formats

> **本原型專用，非正式 AOS ABI。** 不適用composite、directory、agent Function或`.aos`。

## 1. 共通編碼、引用與ID

P1 JSON canonical bytes固定為：

```python
(json.dumps(value, ensure_ascii=False, sort_keys=True,
            separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")
```

- UTF-8無BOM；拒絕invalid Unicode、duplicate／多／缺keys、trailing data、float與NaN/Infinity。
- boolean不是integer；Base64須RFC 4648 canonical。`REF={"sha256":HEX64,"size":N}`涵蓋exact bytes＋newline。
- `HEX64=[0-9a-f]{64}`；root ID `[a-z][a-z0-9_-]{0,31}`且不得含`--`；Task ID同字元、最多64字且可含`--`。Attempt固定`attempt-1`；UUID `[0-9a-f]{32}`；slot為`first|second`。
- `derive_child_id(root,slot) = root + "--" + slot`。Root registry的`root_id`就是root Task ID；recipe內不保存child ID。
- store/P0 directories須是immediate non-symlink directory；authority file須regular non-symlink，拒絕FIFO/device/socket。

Temp只可`.<final>.[A-Za-z0-9_-]{6,32}`。Store：`roots.jsonl,tasks,store.lock`；Task：`call.json,task.json,events.jsonl,attempts,payload`＋temps。`attempts/p0/payload`只容許matching attempt／UUID／`<HEX64>.receipt.json`。見 [`06§3–4`](06-FUNCTION-TASK-MODEL.md)。

Sidecar=store外同層`<store-name>.failpoint-used`；非store／authority，可刪。

## 2. Task檔與events

`task.json` exact object：

```text
{"v":1,"task_id":ID,"call_ref":REF}
```

每個event是canonical object＋newline；不得加seq/timestamp。

`roots.jsonl`只容許：

```text
{"v":1,"type":"root_planned","root_id":ID,"call_ref":REF}
{"v":1,"type":"root_linked","root_id":ID,"call_ref":REF}
```

fixture只容許一個root，嚴格`planned -> linked`；兩者ID/ref相同。

Task `events.jsonl`容許：

```text
{"v":1,"type":"accepted"}
{"v":1,"type":"dispatch_intent","attempt_id":"attempt-1"}
{"v":1,"type":"repair_required","reason":"generation"}
{"v":1,"type":"repair_required","reason":"incomplete_evidence","attempts_ref":REF}
{"v":1,"type":"receipt_committed","receipt_ref":REF}
{"v":1,"type":"child_planned","slot":SLOT,"task_id":ID,"call_ref":REF}
{"v":1,"type":"child_linked","slot":SLOT,"task_id":ID,"call_ref":REF}
{"v":1,"type":"child_observed","slot":SLOT,"task_id":ID,"receipt_ref":REF}
```

Role-specific grammar：

```text
process leaf: accepted -> repair_required(generation)
           or accepted -> dispatch_intent
                       -> receipt_committed | repair_required(incomplete_evidence)
fake leaf:    accepted -> receipt_committed
composite:    accepted -> ordered child_* events [-> receipt_committed]
```

Fake leaf不得有intent／attempt／UUID。`waiting_for_child_repair_generation`、`waiting_for_child_repair_incomplete_evidence`、`waiting_for_parent_policy`都是projection，非event；次序見 [`06§4–6`](06-FUNCTION-TASK-MODEL.md)與 [`07§5`](07-P1A2-PROCESS-SEAM.md)。

Incomplete repair以此canonical tree的REF作`attempts_ref`，不存payload：

```text
{"present":BOOL,"entries":[
  {"kind":"dir","path":REL},
  {"kind":"file","path":REL,"ref":REF}, ...]}
```

`present`表示`attempts/`存在；entries涵蓋全部descendants，按REL UTF-8排序；REL用`/`且不穿越。Repair後ref改變即Contradiction；generation repair要求無`attempts/`。

## 3. Calls與root recipes

下列notation代表exact JSON tree：

```text
BYTES = {"encoding":"base64","data":B64}
BYTES_RESULT = {"termination":TERM,"stdout":BYTES,"stderr":BYTES}

ROOT_CALL = {
  "v":1, "kind":"sequence_two",
  "children":[
    {"slot":"first","recipe":{
      "kind":"process","definition_path":ABS,
      "argv":[STR...],"stdin":BYTES,"cwd":ABS,
      "environment_policy":{"kind":"inherit"}}},
    {"slot":"second","recipe":{"kind":"fake","result":BYTES_RESULT}}
  ],
  "first_success_oracle":BYTES_RESULT
}
```

`kind:"sequence_two"`本身是prototype builtin Definition identity。Recipes／oracle只是fixture Call參數，不是relation／plan／state；schema與golden不加`definition`欄。`children`恰為first、second，recipe無generation／Task ID；oracle須exit 0且empty stderr。

Pre-plan materialize first時產生：

```text
PROCESS_CALL = {
  "v":1, "kind":"process",
  "definition":{"absolute_path":ABS,
                "generation":{"kind":"sha256","hex":HEX64}},
  "argv":[STR...],"stdin":BYTES,"cwd":ABS,
  "environment_policy":{"kind":"inherit"}
}
```

Second產生`{"v":1,"kind":"fake","result":BYTES_RESULT}`。ABS是無NUL absolute POSIX path；argv非空且每項無NUL。本節只屬fixture。

## 4. Return與Task Receipts

TERM只有三種exact variant：

```text
{"kind":"exited","code":I}                         # 0..255
{"kind":"signaled","signal":I,"signal_name":STR}  # 1..255
{"kind":"spawn_error","stage":"spawn","errno":I,"errno_name":STR}
```

signal/spawn name須分別等於`signal.Signals(I).name`／`errno.errorcode.get(I,"UNKNOWN")`。Leaf Return：

```text
RET = {"termination":TERM,"stdout_ref":REF,"stderr_ref":REF}
```

Process leaf Receipt：

```text
{"v":1,"kind":"leaf","task_id":ID,"call_ref":REF,
 "basis":{"kind":"process","attempt_id":"attempt-1",
          "p0_invocation_id":UUID,"evidence_hash":HEX64},
 "return":RET}
```

Fake leaf Receipt：

```text
{"v":1,"kind":"leaf","task_id":ID,"call_ref":REF,
 "basis":{"kind":"fixture_fake","slot":"second"},"return":RET}
```

成功root composite Receipt：

```text
{"v":1,"kind":"composite","task_id":ID,"call_ref":REF,
 "basis":{"children":[
   {"slot":"first","task_id":ID,"receipt_ref":REF},
   {"slot":"second","task_id":ID,"receipt_ref":REF}]},
 "return":{"kind":"fixture_sequence_two_success"}}
```

children順序固定；composite Return不是common ABI。

## 5. P0 raw與evidence manifest

七件evidence固定順序：

```text
request.json, stdin.bin, request-ready.json, dispatch-intent.json,
receipt.json, stdout.bin, stderr.bin
```

P0 JSON可有原pretty whitespace，但仍strict／exact-fields：

```text
request = {"executable":ABS,"argv":[STR...],"cwd":ABS,
           "stdin_encoding":"stored-separately-as-raw-bytes"}
request-ready = {"state":"request_ready"}
dispatch-intent = {"state":"dispatch_intent"}
receipt = {"version":0,"outcome":TERM,"stdout":REF,"stderr":REF}
receipt-ready = {"state":"receipt_ready"}
terminal = {"state":"terminal","outcome":TERM}
```

request/stdin匹配Call，streams匹配raw。Optional ready／terminal匹配receipt但不進manifest。UUID dir只容許七件、兩optional及temps；`outcome_unknown`是corruption。

Manifest不是file，而是fixed-order array：

```text
[{"name":"request.json","ref":REF}, ..., {"name":"stderr.bin","ref":REF}]
```

每個REF hash／size涵蓋該P0檔案exact bytes。計算：

```text
evidence_hash = sha256(
  b"aos-p1a2-process-evidence/v1\0" + canonical_json(manifest))
```

IncompleteUnknown只能是publish prefix：UUID → request → stdin → ready → intent → effect → stdout → stderr → receipt → optional ready → terminal；後件存在但先件缺／壞即Contradiction。

## 6. Prototype bounded limits

- Call／Receipt／task.json／log line／P0 JSON：最多1 MiB。
- stdin、stdout、stderr各最多8 MiB；generation executable最多64 MiB。
- path最多4096 UTF-8 bytes；argv最多256項、每項64 KiB、總Call仍受1 MiB限制。
- 每directory最多64 entries；fixture共1 root、3 Tasks，每process Task 1 attempt／UUID。
- 超限在讀取或配置大型buffer前拒絕；已commit資料超限是corruption。

## 7. Golden vectors

以固定ASCII／binary fixture鎖入tests：

1. ROOT_CALL、PROCESS_CALL、fake Call exact bytes與call_ref。
2. 每種event exact line；incomplete repair含attempts tree/ref；fake只有`accepted`、`receipt_committed`。
3. 七件raw refs、manifest bytes與evidence_hash。
4. process/fake/composite Receipt exact bytes與receipt_ref。

Expected須checked-in，不能同法算兩邊；格式變更須升version。
