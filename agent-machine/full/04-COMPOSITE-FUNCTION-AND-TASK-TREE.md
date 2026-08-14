# Composite Function 與 Task tree

本頁只回答一件事：一個 Function 呼叫其他 Functions 時，AOS 如何保留父子關係並組合結果。角色名稱先依[03](03-CALL-TASK-RETURN.md)，process child 的底層證據依[02](02-LEAF-PROCESS-CALL.md)。

## Function 可以呼叫 Function

### 使用者已定

- Function 能呼叫其他 Functions，所以大型工作可由較小工作組成。
- Linux process 只是 leaf；composite Function 不是「比較大的 process」。
- Step 與 Round 也都是 Function，但它們的 Agent 角色及原子邊界不由本頁補定。

### 目前推薦

```text
parent Function Call
  -> child A Call -> child A Return
  -> 依 parent policy 決定下一步
  -> child B Call -> child B Return
  -> parent 自己的 Return
```

- 普通 library call 或 directory 內 helper 不會自動變成 child Task。只有 child 需要 AOS 獨立保存、觀察、暫停、排程或處理外部作用時，才建立 AOS-visible child Call／Task。
- 同一個 Function 可被呼叫多次；每次 child Task 是不同執行實例，不能以 Definition path 或 Call hash 代替 Task 身分。
- Parent 的 Return 是自己的語意結果，不是把 children 的 stdout 直接串起來。

## Tree 必須由權威關係建立

### 目前推薦

- Top-level Task 是 tree root；parent 明確記下 child relation。不得掃 `tasks/`、Function directory 或 OS process descendants 猜一棵 tree。
- Task-tree root、Agent root 與 Linux process tree 是三件事：前者是呼叫關係，第二個是 Agent 的絕對 path 地盤，第三個是作業系統親子 process。
- Relation 至少要能回答 parent、child、呼叫位置或順序，以及 parent 已觀察哪份 child 結果。Exact event 與 backlink 不是本頁契約。
- Child 被持久地納入 relation 且自身資料完整後，才可執行；先跑再補關係會讓 recovery 無法分辨孤兒與合法 child。
- 沒有 authority 指向的目錄只是 orphan evidence，不因內容看似完整就可 dispatch。

第一版以 tree 為主：每個非 root Task 有一個明確 parent。共享 child、joinable DAG 或跨 tree 引用若日後需要，必須另定 authority，不能偷偷共用 Task ID。

## 從 child 結果組成 parent 結果

### 目前推薦

安全順序是：parent 先保存 child intent／relation，child 取得已知 Return 與可信持久證據，parent 重驗後才記為 observed，最後才提交 parent Return。

- Child Return 符合 parent policy，parent 才規劃下一步。
- Child 有完整但不符合預期的 Return，parent 應保存它，再進入明確 policy；不能把已知失敗改名為 unknown。
- Child 結果 unknown 時，parent 不得假裝觀察到成功，也不得自動重做可能有副作用的 child。
- Parent 的持久證據應能引用所依賴的 ordered child evidence。它只證明已驗證的組合，不承諾外部世界可 rollback。
- 每跨過一次會造成外部作用或讓上層看見完成的邊界，都先完成前一個 durable commit；精確檔案與 event grammar 留給持久化頁決定。

### 尚未決定

- Common composite Return ABI、child failure 對 parent 的預設 mapping，以及 unknown／repair 的公開操作。
- Sequential、fan-out、join、取消、pause、timeout 與 resource propagation 的完整規則。
- 是否所有 AOS-visible child 都建立 Task、Task 正式起點，以及 child ID 與 relation 的保存格式。
- Task tree 的排程先後與 filesystem 寫入權。Tree 表示依賴，不會自動授予容量、並行安全或 Agent root 所有權。

## 原型證據的停止線

### 原型暫選

- [P1a-1 v2](../workbench/2026-08-14/p1a-task-tree-python-v2/README.md) 以 fake executor 驗到：Call 不含 relation、每個 Task 自存 Call、parent 可用有順序的記錄建立 child relation，known child result 重開後不重做。
- [P1a-2A](../workbench/2026-08-14/round-2-decision/09-P1A2-COMPOSITE-FAKE.md) 的 `sequence_two_fake` 已在 WSL 通過 58 項測試。它覆蓋 root 接受、兩個 fake children、10 個 root 與 23 個 composite process-kill 切點、孤兒與竄改反例；terminal recovery 保持相同 authority bytes。
- `first`／`second`、固定 child ID 推導、`planned`／`linked`／`observed` event、Receipt JSON、容量 64、eager fake runner 與 `<store>` 都只屬 fixture，不是 ABI。
- 已通過的這片沒有真 process、正式 scheduler、Agent、Step／Round、portable checkpoint、power-loss、NFS 或多 writer 證據。
- 現階段可相信的是「明確父子關係與持久順序能被小原型驗證」，不是「原型的檔名、ID 或 CLI 已成為產品介面」。

### 明確延後

- 跨機 Task tree、共享 DAG、多 writer tree、私有 workspace 的並行發布，以及把外部 side effects 做成可回滾 transaction。
