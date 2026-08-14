# 已知觀察與 blockers

以下是現有 Python P1a-1 原型已跑過的 crash/failpoint 證據摘要，不是正式 durability 保證：單一 writer 在同一 Linux filesystem 上，於命名 failpoint 強制結束、再多次 restart；固定兩格 composite（`first -> second`）可驗成功與 unknown。

- `child_planned -> materialize -> child_linked` 前，child 不 eligible；同一 parent/slot 使用 deterministic ID，恢復不另生身分。
- 已有可信 semantic Receipt 時，recovery 不增加 dispatch count；若 `dispatch_intent` 後沒有完整可信 Receipt，進 `repair_required`，不自動重送。
- first unknown 時，parent 也停住，second 不得建立或派送；parent Receipt 只引用 ordered child receipt hashes，不複製輸出。

已知 blockers／尚未驗證，請**不用重找**；只有結論不同才展開：

1. `planned`／`linked`／`observed` 跨 Task 關係、receipt 語意與完整 event ordering 尚未被獨立驗證。
2. 現原型讓 parent 唯一保存 child Call，child 只引用該路徑+hash；這造成 life-cycle coupling，故 P1a 暫薦改為 child 自存 Call。
3. root/parent 自己的 Call acceptance 尚未完整模型化，可能出現 parent Task 沒有 Call。
4. Call record 與 Task/tree state 的欄位仍有混放，report 的 phase 與 Task tree 表達也不夠清楚。
5. symlink component 只做拒絕／偵測，尚非完整 path security 或可攜搬移模型。

P1a-2 尚未做；它只應驗「P0 staging 完整、但 child semantic Receipt 尚未 commit」時可補 commit 而不重跑 process。它不驗 power loss、NFS、多 writer、exactly-once、rollback、Git 或跨機。
