# OPUS-REVIEW：P1a 架構裁決

## 0. 一項前提更正

`SOURCE-PATHS` 指向 v1，但同層已有 `p1a-task-tree-python-v2`：它已把 child Call 改成自存、Call/Task/relation 三分、root 也有自己的 Call、report 改成 nested tree。KNOWN-FINDINGS 的 2 與 4 已被 v2 解決，3 只解一半。以下裁決以 v2 為對象，v1 視為已被取代；請更新閱讀路徑。

## 1. Child Call 保存位置：採 A，A→C 合理

**可接受**（A：每個 child 自存 Call）。

理由不是「省重複 bytes」的取捨，而是所有權：B 讓 parent 的保存期變成 child 正確性的前提，且每次 dispatch 都要跨 Task 讀 parent；parent 一進 terminal 或被清理，child 的意義就殘缺。Task 必須能只讀自己的目錄就說明它被要求做什麼。重複的 Call bytes 有界且極小，不值得用生命期耦合去換。

A→C 是合理的分階段路徑，但只在一個條件下成立：**`call_ref{hash,size}` 必須被宣告為 Call 的唯一身分，`call.json` 只是其中一個解析位置**。保持這個介面，改 CAS 就只換解析器、不動 schema。現在程式把位置寫死在 `folder/"call.json"`，P1a 沒問題，但文件要明說「hash 是身分、位置是政策」。

## 2. Call／Task／relation 切分：可接受，但缺一個入口記錄

切分方向正確，而且 v2 有一個很有力的可測不變量：first 與 second 的 `call.json` bytes 完全相同、Task ID 不同——這實證了 relation 沒有滲進 Call。Task 記錄只留 `call_ref`、關係只在 parent ordered events、child 無 backlink、不掃目錄，這幾條請保留。

回答你的問題：**「每 Task ordered events + parent `planned→linked→observed`」足以支持向下 replay，但不足以回答「從哪裡開始 replay」**。目前 root ID 是常數，`initialize()` 在每次 recovery 又被重跑；一旦不是 fixture，recovery 就只剩「掃 `tasks/` 找 root」這條被模型禁止的路。這才是 KNOWN-FINDINGS 3 的真正內容，不只是「parent 可能沒有 Call」。修正見 B2。

另外請注意：root 接受在結構上與 child publish 同形（call → task → accepted → 由更上層記錄 link）。把 store 的 root registry 當成 parent 的角色，root 就不需要特例。

## 3. Callable path 與 P1a-2 門檻：主線合理

「path 是 Definition、Call 是已解析的固定請求」可接受，而且它正好排除 registry 與 PATH 搜尋。最低要求三條：

1. 入口明示：leaf file 即自身；directory module 必須在內部明寫入口，不得猜 `main.py`。leaf 長成 directory 時，呼叫者輸入的 path 不變，變的只有解析結果——`aos exec ./ask` 因此可穩定。
2. 解析只發生一次，在 accept 當下：相對 path 依呼叫者 cwd 解析成絕對 path 才寫進 Call（v2 已強制 Call 內 definition/cwd 為絕對，請保留）。
3. `definition_generation` 在 P1a-2 必須是真的算出來的：解析後入口檔的 content hash。recovery 一律用 planned 當時的 Call，不重新解析；與 generation 不符即 fail closed，不自動重跑。directory module 的整樹身分請明確排除在 P1a-2 外，只釘入口檔。

## Blocking（4 項）

**B1. staging 期的 `call.json` 不可當 immutable authority。** 現在 `child_stage()` 先原子寫 child 的 `call.json`，而 `atomic()` 遇到已存在但 bytes 不同即報 corruption。P1a-1 因 Call 是常數永遠踩不到；P1a-2 一旦 generation 是真檔案 hash，「crash 落在 stage 之後、`child_planned` 之前，其間 definition 被改」就會讓一個從未被 plan 過的 slot 永久卡死整個 store（validator 看不到未 planned 的目錄，progress 每次都撞同一個 corruption）。修正：只有 `child_planned` 賦予不可變性；在那之前的 staged bytes 是可丟棄的垃圾，不同即覆寫或整個 staging 目錄丟棄，並補一個 failpoint test。

**B2. 必須有 durable root 記錄。** store 層加一條 append-only 的已接受 root log，寫在 root Task materialize 之前；recovery 只從它列舉 root，永不掃 `tasks/`。同時 `initialize()` 不應在 recovery 路徑重跑——accept 是一次性持久轉移，recovery 只該讀。

**B3. Call 必須帶解析後的 Definition 身分，recovery 不得重新解析。** 見第 3 節第 2、3 點。缺這條，P1a-2「驗證後只補 commit、不重跑 process」的承諾就沒有可驗證的基礎。

**B4. 明文寫下 `call_ref` 是唯一身分，以及 orphan 規則。** unlinked staging 目錄既不是 Task 也不是 corruption；P1a-2 不必實作 GC，但必須寫下由誰、何時清理，否則 CAS 階段會繼承一堆沒有主人的垃圾。

## 仍需保留的風險（不 blocking）

- parent 的 `repair_required` 目前在 validator 要求「必有 child 在 repair」。composite 一旦有自己的 effect，這條文法會擋住合法情況，預期要改。
- torn tail 可截斷的前提是「dispatch 只發生在完整 event line fsync 之後」。接真 process 後必須用真 staging evidence 重驗，不可沿用。
- 沒有 backlink 是對的；未來「child 完成要喚醒 parent」只能建可重建的 derived index，不得成為第二真源。
- `validate_store()` 在每次 transition 前後全量重驗，P1a 可接受，但別把它當可擴充的讀取路徑。
- symlink 檢查是 lstat 級，不是 TOCTOU 安全模型；NOTES 已承認，對外文件請勿升級措辭。

## 裁決

**准許 P1a-1（v2）進入 P1a-2**。條件：B1 與 B2 在 P1a-2 之前完成、各補一個 failpoint test；B3 隨 P1a-2 的第一個 process case 一起落地；B4 只需寫進文件。P1a-2 範圍維持「只換一個 leaf 為 deterministic P0 staging evidence」，不要順手帶進 Definition resolver 以外的東西。
