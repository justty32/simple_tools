# 檔案空間、交易、Git 與回收

## 對外是檔案，對內是已驗證操作

VFS 是 Agent Machine 的統一位址空間。RAM、本機磁碟、Git tree、LLM endpoint、network service
或 device 都可掛成節點；使用者用 path、list、read、write 組合它們，底層儲存位置不是 API。

但 `write(".bot-a/state", bytes)` 不會直接覆蓋權威檔案。VFS provider 先把它解析成
`UpdateFunctionState` 等 typed operation，檢查 schema、root generation、actor、權限與限制後，才進交易。
普通資料檔可以有普通 byte-stream 語意；控制節點必須宣告支援的操作，不能藏自然語言後門。

第一版 provider 介面：

```text
resolve(path, view) -> NodeHandle
stat(handle) / list(handle, cursor, limit) / read(handle, range)
begin(expected_root_generation) -> Transaction
stage_write(tx, handle, bytes) / stage_remove(tx, handle)
commit(tx) -> CommitRecord
abort(tx)
```

`NodeHandle` 綁 `node_id + snapshot root generation + actor + capability/policy generation`。名稱相同不代表
舊 handle 會自動指向新物件；ACL revoke 後也不能永久可寫，每次 commit 都重新授權。

## Function 與 companion directory

以下是 **illustrative v1 companion schema**，不是全系統固定目錄：

```text
bot-a                       可呼叫的 Function 節點
.bot-a/
  format.json               schema 名稱與版本
  config.json               executor、model、parameters、policy refs
  state.json                小型目前狀態；大型內容只放 ref
  refs.json                 此 Function 明確持有的 references
  inbox/                    尚未接受的輸入
  rounds/<round-id>/        各次呼叫的狀態、manifest、結果
  journal/                  本 Function 相關的操作索引
```

這只是第一個會自然長大的例子，不是規定全機器使用固定 Unix layout。規則只有：

- `name` 與同層 `.name/` 是一組；建立、rename、move、delete 必須在同一交易處理兩者。
- `.name/` 是保留配對名稱；已被一般目錄占用時建立 Function 要失敗，不偷偷覆蓋。
- 公開 callable identity 仍是 `name`。rename 改 path，不改 `function_id` 或 `node_id`。
- companion 可以不存在；第一次需要狀態時才建立。
- 巢狀 Function 套用相同規則，不需先設計完整樹。

Python 原型先用 `agent-machine run path/to/bot-a -- "do something"`。最終 native launcher 讓
`./bot-a "do something"` 成為普通 shell 呼叫；pipe、redirection 與 exit code 沿用 host shell。

## 多檔案交易與 crash recovery

host filesystem 無法安全地原子覆蓋整棵樹，因此交易採 copy-on-write：

```text
G -> 建 staging/G+1 -> 寫內容與 manifest -> fsync files/directories
  -> journal: prepared -> fsync journal
  -> 原子 rename HEAD(G -> G+1) -> fsync HEAD parent directory
  -> journal: committed -> 非同步清理 staging
```

`HEAD` 是很小的 root-generation ref。v1 由單一 commit loop（跨 process 時再加 OS lock）序列化，
驗 expected G 後以 atomic rename 發布；普通 filesystem rename 本身不是 CAS。Linux durability 要包含
file 與 parent-directory fsync；其他平台只能宣告實測過的較弱保證。讀者只看完整 G 或 G+1。
啟動恢復規則固定：

- 沒有 `prepared`：staging 是垃圾，可封存。
- 有 `prepared`、HEAD 仍是 G：交易未發布，可重試 commit 或 abort。
- HEAD 已是 G+1、缺 `committed`：補記 committed，不重做 executor effect。
- manifest/hash 不合：fail closed，保留現場並要求 repair。

Journal 是 write-ahead recovery record，不是另一份可任意修改的 current state。Snapshot 只加速載入，
必須可由已提交 generation 與 journal 驗證。

## Git 的明確責任

Git 只版本化 mount policy 標成 `versioned` 的節點，預設包含 Function 定義、prompt、tool spec、
policy、測試與使用者工作檔；queue、短 lease、心跳、cache 和高頻 metrics 不為每次變動建立 commit。

預設 checkpoint 邊界：一個使用者可理解的操作完成、Round 到達安全邊界、版本 promotion，或使用者
明確要求 checkpoint。read 不 commit，每個 token/Step 也不強制 commit。

VFS HEAD 是唯一提交真源；Git managed ref 是其可修復 projection，兩者不能假裝跨系統原子：

```text
stage/validate VFS -> build Git commit object（不更新 ref）
-> durability sync + fresh-process verify git_oid
-> prepared journal 保存 candidate G+1 + git_oid
-> publish VFS HEAD(G -> G+1, git_oid)
-> CAS publish managed Git ref -> journal checkpoint complete
```

Git object 成功的定義是 commit、tree、全部新 blob 與必要 loose-object／pack index 已同步到 target
object database，且 fresh Git process 不讀工作樹也能驗證 `git_oid`。只有此後 HEAD 才能引用它。
Recovery 若發現 HEAD 的 oid 缺失／損壞，標成 `checkpoint_corrupt` 並 fail closed；不得重跑 executor
或用當前工作樹猜測重建。

若 crash 發生在 HEAD 後、Git ref 前，generation 標成 `checkpoint_pending`；recovery 驗證 Git object
後重試 ref publish。Git ref 已前進但 HEAD 未前進是 repair case，不重跑 executor。若 ref 被外部更新，
VFS commit 仍有效但 checkpoint 報 conflict；merge 是新的可審查 operation，不能偷偷覆蓋。
Git 能把 active version ref 指回舊 revision；不能取消網路／device effect，也不取代 resource ledger。

## 外部資源

Network、service 與 device 可以掛入相同 namespace，但 provider 要標明：

- read 是否穩定、是否需要把 observation 保存進本次 manifest；
- write 是否可重送、是否有 idempotency key；
- 能否查詢結果、補償或撤銷；
- 誰有權執行，以及輸入／輸出大小和 timeout。

外部 write 使用 `intent -> dispatch -> receipt/unknown -> settle`。無法原子提交時，VFS 保存的是
效果紀錄與目前已知狀態，不假裝遠端世界被 Git 包進交易。

`base_tools`、exec 或 HTTP 若直接改 host world，都屬這類外部效果。需要讓 tool 產生可驗證 VFS 修改時，
給它 staging workspace；核心讀 diff，以新的 import operation 驗證後發布。但在 Linux sandbox
落地前，這個 workspace 只是候選輸出位置，不限制 process 的絕對路徑、網路或其他 host effect，
不能稱為隔離，也不能把任意 host write 說成已 rollback 的 `proposed_changes`。

## refs 與垃圾回收

`.bot-a/refs.json` 是該 Function 的明確 root，但不是唯一 root。live roots 還包括 active Round、
執行中 lease、使用者 pin、system retention、尚未過期的 archive 與被保留的 Git version。

回收固定分四階段：

1. 在 root generation G 取得 roots snapshot，走訪 node-id graph。
2. 將不可達物件列成 GC proposal；先 dry-run，不能直接刪。
3. 重新確認 root generation、lease、pin 與 policy generation 未變後，移到 quarantine/archive。
4. grace period 後才 purge；Git object GC 與 live VFS GC 分開執行。

共享與循環 refs 都用 reachability 處理。外部資源預設只移除本機 reference，不刪遠端物件；只有
provider 明確宣告 ownership 與 delete capability 時才可另發刪除 operation。
