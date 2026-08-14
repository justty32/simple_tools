# P1a-1 v2：Task tree 的可驗證假執行器

> 工作台實驗，等待 Opus 裁決；不是正式 AOS 規格或 durability 承諾。

本目錄重寫 P1a-1 的資料模型與 replay，而不是在舊事件種類上打補丁。它驗證固定 composite Function `first -> second` 如何把不可變 Call、Task、parent/child 關係與 Receipt 分開保存。fake leaf 不啟動 subprocess。

程式分成三層：`p1a_store.py` 放 strict JSON、hash/canonical bytes 與基礎 filesystem primitive；`p1a_model.py` 放 fixture schema、validated Projection、replay/progress；`aos_p1a_v2.py` 只保留公開 CLI/report 入口。沒有第二份狀態真源。

## 快速重跑

請用 WSL 的 Linux filesystem（`/tmp`），不要把 `/mnt/c` 當 durability 證據：

```bash
cd /mnt/c/code/mine/simple_tools/agent-machine/workbench/2026-08-14/p1a-task-tree-python-v2
python3 -m unittest -v
python3 aos_p1a_v2.py --root /tmp/aos-p1a-v2-demo --case success
cat /tmp/aos-p1a-v2-demo/report.json
```

每個 failpoint 的首個 writer 以 `os._exit(97)` 中止；測試隨後開十個新的 Python process 做 recovery。這只涵蓋同一 Linux filesystem 的 process-kill/failpoint，不涵蓋拔電、磁碟快取、NFS、多 writer、安全 TOCTOU、exactly-once 或外部效果回滾。

## 這版的核心切分

```text
Function Definition + immutable Call --接受--> Task --已知完成--> Receipt

composite root Task
  parent event: child_planned(slot,id,call_hash)
  child own directory: call.json -> task.json + accepted
  parent event: child_linked
  child: dispatch_intent -> receipt_committed | repair_required
  parent: child_observed -> … -> receipt_committed | repair_required
```

- **Call** 是純請求：Definition path/generation、argv、stdin、cwd/env、fixture policy。它不得帶 `task_id`、`parent_id`、slot 或 state。
- **Task** 的 immutable `task.json` 只有 `schema/task_id/call_ref(hash,size)`；每個 Task 自己保存唯一的完整 `call.json`。
- **關係** 唯一由 parent ordered events 保存。child 無 parent/slot backlink；不得掃描 `tasks/` 猜 tree。
- first 與 second 的 leaf `call.json` bytes/hash 刻意完全相同，但 Task ID 不同；unknown 是 harness/executor 情境，不能混入 Call。
- **Receipt** 是執行記錄而不是 Function Return；P1a 將 Return 內嵌於 Receipt。unknown/repair_required 沒有 Receipt。

Call 的 child 發布次序是：child own `call.json` 原子發布（暫時 orphan staging）→ parent `child_planned` → child `task.json` + `accepted` → parent `child_linked` → eligible。Call 的本地位置只由 child ID 推導；相同 ID 出現不同 hash 立即 corruption。

`task.json` 已落盤但 `accepted` 尚未寫入是 process-kill 時的合法中間狀態：recovery 只能先補 `accepted`，重新驗證後才可 link。planned 但尚未 linked 的 child events 只允許空或唯一 `accepted`；任何 intent、Receipt、repair 或其他事件都 fail closed，不能用「先 link 再接受」掩蓋早跑。

## Validator 與報告

唯一 read 入口是 `validate_store()`，產生 validated Projection。每次 `progress_once()` 只做一個合法的持久 transition，隨即再驗；report 也只讀 Projection。它嚴格拒絕 duplicate JSON key、NaN、未知/缺失欄位、事件逆序/重複/terminal 後追加。沒有 newline 的最後一段只在**recovery/progress** 被當作未提交 torn tail 截掉並 fsync；純 validation/report 絕不改 events，換行結尾的壞 JSON 永遠 corruption。這在 P1a 可行，是因為 dispatch 只在完整 event line fsync 後發生；P1a-2 仍要以真 staging evidence 重驗此假設。

更精確地說，validator 先只讀每份 log 的 committed prefix，將固定 log path 與 prefix length 放進成功的 Projection；只有所有 parent/child relation、schema 和 Receipt 都通過後，recovery 才依此清單 truncate+fsync，並重新驗證。因而壞 parent 不得提前修改 child 的 torn tail。所有要讀的 Call、Task、event、Receipt、fixture/report authority file 都會先以 `lstat` 確認是 regular file 且不是 symlink；FIFO/socket/device/directory 直接回報 corruption，不會拿去 `read_bytes()`。

`report.json` 是真正 nested tree，先顯示 definition path、argv 摘要、slot、state、Return status，再帶 dispatch_count、receipt、reason 與 blocked_on；phase 區分 `run`、`recovery`、`corrupt`。report 不綠即表示 validator 已拒絕 store。若 lexical root 本身是 symlink，程式不會寫穿它，而在同層產生 `<root>.corrupt-report.json`；下次正常 report 會安全移除此 stale sidecar。

## 範圍與下一步

P1a-1 v2 只用單一 fake executor。intent 後沒有完整可信 Receipt 時 child 與 parent 會 repair_required、絕不重送；有完整 staged Receipt 則只補 semantic commit。P1a-2 是否值得做，取決於 Opus 是否接受這個切分；若接受，下一片只替換一個 leaf 為 P0 staging evidence，不同時接 Definition resolver、Agent、Git/CAS、中央 Runtime 或多 writer。

`fixture-*.json` 與 `schema-example.json` 是供閱讀的 illustrative fixture/example；目前的 oracle 直接驗真正 store 的 bytes、hash 與事件序，不能把它們誤當 golden store。
