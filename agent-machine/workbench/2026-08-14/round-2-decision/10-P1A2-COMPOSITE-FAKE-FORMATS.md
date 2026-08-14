# P1a-2A fake 串接：精確工作台格式

> **只屬 `sequence_two_fake` 工作台，非正式 AOS ABI。** 既有 `sequence_two` 與 [`08-P1A2-FORMATS.md`](08-P1A2-FORMATS.md) 完全不改；本頁只寫它新增的 delta。

## 1. 固定 fake 結果與 Call

沿用 08 的 `TERM`、`BYTES`、`BYTES_RESULT` 與 canonical JSON 規則。default golden 固定：

```text
OK = {"termination":{"kind":"exited","code":0},
      "stdout":{"encoding":"base64","data":"b2sK"},
      "stderr":{"encoding":"base64","data":""}}
```

它代表 `stdout == b"ok\n"`、`stderr == b""`。default fixture 的 first recipe、second recipe、`first_success_oracle` 都必須是同一個 `OK` tree；oracle mismatch 是另一個明列的測試，不可偷改 default golden。

新增 root Call 的 exact tree：

```text
ROOT_CALL_FAKE = {
  "v":1, "kind":"sequence_two_fake",
  "children":[
    {"slot":"first", "recipe":{"kind":"fake","result":OK}},
    {"slot":"second","recipe":{"kind":"fake","result":OK}}
  ],
  "first_success_oracle":OK
}
```

`children` 順序固定 first、second，不能有別的欄位。child Call 都是 08 已有的 exact fake Call：

```text
FAKE_CALL = {"v":1,"kind":"fake","result":OK}
```

所以 first、second 的 Call bytes/ref 在 default golden 中相同，但 child Task ID 不同。兩個 child ID 仍用 08 的 `derive_child_id(root_id,slot)`；root ID 不得含 `--`。

## 2. Task event grammar 與 Receipt

roots registry 仍只有：

```text
root_planned(root_id, call_ref) -> root_linked(root_id, call_ref)
```

root Task event grammar：

```text
accepted
-> child_planned(first) -> child_linked(first) -> child_observed(first)
-> [child_planned(second) -> child_linked(second) -> child_observed(second)]
-> [receipt_committed(root)]
```

中括號條件是：first observed 的 fake Receipt 符合 `first_success_oracle` 時才允許 second；兩個 child 都 observed 時才允許 root commit。first 已知但 mismatch 時，event stream 正好停在 first observed，投影為 `waiting_for_parent_policy`。

每個 fake child 的 event stream 只有：

```text
accepted -> receipt_committed(receipt_ref)
```

不得有 `dispatch_intent`、`repair_required`、`attempts/` 或 P0 UUID。

fake leaf Receipt **不新增格式**，沿用 08：

```text
{"v":1,"kind":"leaf","task_id":ID,"call_ref":REF,
 "basis":{"kind":"fixture_fake","slot":SLOT},"return":RET}
```

其中 `SLOT` 為 first 或 second。`RET` 由 recipe 的 `BYTES_RESULT` 導出：termination 相同，`stdout_ref`／`stderr_ref` 分別是 decode 後 raw bytes 的 REF。成功 root composite Receipt 也沿用 08 的 exact tree（children 順序 first、second；Return 固定 `{"kind":"fixture_sequence_two_success"}`）。因此本片的 Receipt delta 只有：first 現在也是上述 fake Receipt；沒有 process basis、attempt 或 P0 evidence。

## 3. Candidate、關係與容量

這裡的「可達」與「可執行」不同：`child_planned` 才使 child 可達；`child_linked` 後才 eligible。未 planned 的目錄不能靠掃描變成 child。

### 接受前

解析並驗 root fake Call 後、首筆 authority write 前，檢查 `tasks/` 直接 entries：

```text
現有 entries + 尚缺的 root/first/second Task 目錄 <= 64
```

61 個 unrelated orphan 時，三個預定目錄可剛好成為 64；62 個以上必須拒絕，零 authority write。若 root 尚未 planned，而任一衍生 child ID 目錄已存在，直接 `StoreError` 拒絕並保留原 bytes；不得把它當可覆寫 staging。

### root linked、下一 slot 尚未 planned

該 child directory 只可為：不存在、空目錄，或只含 regular non-symlink `call.json` 與合法 atomic temp。它是可重建 staging；實作者可完全由 frozen root Call 重新產生 child Call，並以 staging publish 覆蓋該 Call。

出現未知 entry、`task.json`、`events.jsonl`、`payload/`、非 regular 檔或不合法 temp，都是 **staging collision**：`StoreError`、本次零 write。child 一旦 planned，缺少 Call、Call ref/bytes 不同或有不合格式的內容都是 **corruption**，不得重建。

每次 `mkdir` 前都要重新檢查目錄 capacity。每次建立 atomic temp 時，只接受 `current_entries + 1 <= 64`；不符合就不能建立 temp。外力在預檢後塞滿時，此次 transition 回報 `capacity unavailable`、不寫新的 authority；它不是 terminal corruption，也不能越過此 gap。

root 在 Phase 1 已有 Task directory；本片新增或會跨越的目錄建立切點共五次：root `payload/`、first Task directory、first `payload/`、second Task directory、second `payload/`。

## 4. Golden 與 crash 測試

default golden 必須在 tests 中 checked-in 為**literal UTF-8 bytes 與 literal `{sha256,size}` refs**；expected 不得呼叫 production canonicalizer、hash helper 或 Receipt writer 來產生。至少鎖定：

1. `ROOT_CALL_FAKE`、first/second `FAKE_CALL` 的 bytes/ref。
2. root、first、second 的 `task.json` bytes/ref。
3. roots log 的兩行；root/child 全部 event line 與最終 JSONL bytes。
4. first fake、second fake、root composite 三份 Receipt payload bytes/ref。
5. 完整 clean store 的 authority file list、每檔 bytes/ref，及預期的可達/eligible tree。

新 crash cuts 共 23：每個 child 各 10（Task dir、payload dir、Call publish、parent plan、task.json、accepted、parent link、Receipt payload、child commit、parent observe），再加 root 3（root payload dir、root Receipt payload、root commit）。每個切點都必須精確 exit 97，重新開新 process recover，最後 authority bytes 等於 default clean golden；另跑五次 terminal recover，bytes 仍不變。

還要覆蓋：原 input 刪除/FIFO/poison resolver 後 recover、orphan 不進 tree、planned 後 Call 缺失或不同、event order/ref/payload tamper、容量 61/62、及 first oracle mismatch 不建立 second/root Receipt。

本頁未授權 eager fake runner 成為 scheduler。它只是工作台中為了把合法下一 transition 做完的測試驅動；agent 的 busy admission（拒絕、追加或條件 queue）另有自己的 Task/寫入權規格，不能由 child materialization 推導。

相關流程見 [`09-P1A2-COMPOSITE-FAKE.md`](09-P1A2-COMPOSITE-FAKE.md)；既有共通格式只見 [`08-P1A2-FORMATS.md`](08-P1A2-FORMATS.md)。
