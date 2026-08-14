# Filesystem Memory 候選

> 讓「filesystem 像記憶體」可實作的候選，不是已定保證。

## 不要用一種模式包辦所有檔案

agent root 內至少有五類資料：

1. Function Definition：這次到底使用哪版程式。
2. agent 可變記憶：messages、設定、成果與自我修改。
3. AOS 證據：Call、Receipt、events、checkpoint。
4. 本機即時狀態：PID、lock、worker 與工作領取記錄。
5. 外部世界：HTTP、寄信、child agent 與 root 外檔案。

它們不適合共用同一套 live 或 snapshot 規則。建議把三個選項分開思考：

```text
讀取來源：目前路徑 / 固定快照
版本保護：不檢查 / dispatch 前檢查 / commit 前檢查
寫入位置：公開 root / staging / 私有 branch
```

## 四套整體方案

### 方案一：Live filesystem

Function 每次都讀目前路徑，修改也直接寫公開 root。

優點：最符合 shell 習慣；手動編輯與 agent 自我修改很自然；實作成本低。

缺點：同一 Task 前後可能看到不同內容；難以重現；process 被殺時可能留下半份檔案。

### 方案二：Checked-live

仍使用普通目錄，但讀取時記住版本；dispatch 或發布修改前重新比較，版本已變就停止。

C++ 類比：先讀版本，提交時用 `compare_exchange` 的思路擋下過期寫入。

優點：保留普通目錄操作，也能防止舊 Task 默默覆蓋新狀態。

缺點：檢查到 `exec` 之間仍可能換檔；外部效果完成後才發現版本衝突，也無法撤銷。

### 方案三：Snapshot／branch／overlay

Task 讀固定副本，並在私有工作區修改；最後比較 base version 再發布。

優點：穩定、容易重現，也適合同一 agent 平行試做多個方案。

缺點：發布、衝突、清理與磁碟成本高；寄信或 API 等外部作用不會因丟棄 branch 而消失。

### 方案四：混合模式

- agent root 保持 live，維持 shell 操作體驗。
- 同一 root 一次只有一棵可寫 Task tree。
- Function dispatch 前檢查 Definition version。
- 記憶修改先寫 staging，再比對 base version 後發布。
- 已提交的 Call、Receipt、events 與 checkpoint 不再修改。
- 外部作用另走 intent／Receipt，不宣稱可 rollback。

優點：保留 AOS 的直覺，可靠度也能逐步增加。

缺點：每一區資料必須清楚標示採哪條規則，不能只寫一個模糊的 `memory_mode`。

建議：第一版採混合模式，完整 workspace overlay 留到後期。

## 三種版本世代（generation）必須分開

```text
definition_generation  本次 Function 使用哪版程式
memory_generation      agent 已提交到哪版記憶
runtime_generation     目前是哪次 AOS 啟動與本機執行世代
```

整棵 agent root 不適合直接當 Definition hash：寫一則 message 就會讓自己的程式看似過期。只 hash `./ask` 也不夠，因為它可能載入 helper、prompt 或 config。

Directory Function 應用入口設定檔（manifest）明列 Definition inputs；mutable memory 不放進 Definition generation。入口方案見 [`MODULE-EVOLUTION-CANDIDATES.md`](MODULE-EVOLUTION-CANDIDATES.md)。

P1a-2 的 leaf hash 只驗啟動前內容，不能當完整 directory 版本。

## 自我修改的建議語意

```text
Task T 使用 Definition D7、Memory M12
  -> 執行並產生 Memory M13
  -> 也可能提出新 Definition D8
  -> 只有目前 Memory 仍是 M12，才可發布 M13
```

Receipt 應表達「T 以 D7、M12 執行，提交 M13，並產生供後續使用的 D8」。不能把 D8 說成本次已執行的 Definition。

若 T 真要呼叫剛產生的 D8，先完整發布 D8，再建立明確綁定 D8 的 child Call。這保留自我修改能力，也不會在半途偷換已接受 Call 的意思。

可另提供顯式 live 模式，允許同次執行看見即時修改；代價是 Receipt 必須標明不可重現。

## Path、已開啟檔案與 Hash 不相同

典型空窗是：

```text
檢查 ./ask hash=A
寫入 dispatch intent
外部把路徑換成 B
execve("./ask")
實際可能執行 B
```

因此 `path + hash-before-exec` 只證明檢查當下是 A，不證明實際執行 bytes。較強版本可研究 snapshot 或固定已開啟的 fd，但 native ELF 成功也不能直接外推到 `#!` script、shared library 與 helper。

其他不能被抽象名稱遮掉的 Linux 事實：

- path 是目前名稱；fd／inode 是已開啟物件；hash 是內容版本。
- `write()` 成功不代表斷電後仍存在；還需 file fsync、rename、directory fsync。
- atomic rename 只有一個發布點，不會自動變成任意多檔 transaction。
- `cwd` 只改相對路徑起點，不是 filesystem sandbox。
- process 結束不代表它 fork 出去的 background child 已結束。

## 外部作用不在 Snapshot 裡

所有模式都會遇到：請求已送出，外部已完成，但 AOS 在 Receipt 前 crash。底線候選是：

```text
effect intent durable -> 執行 effect -> effect Receipt durable
```

只有 intent、沒有完整 Receipt 時標成結果不明（unknown），不自動重做。服務若支援防重複 key，可用同 key 查詢或重送；這是服務提供的能力，不是 snapshot 自動給的保證。

## Pause 與安全存檔門檻

至少分開：

- `pause-requested`：已要求停下，仍可能有工作。
- `paused-safe`：可建立能續接的 checkpoint。
- `diagnostic-only`：只能保存證據，不能自動續跑。

`paused-safe` 的最低條件候選：

- 沒有仍會寫 agent root 的 leaf process，受管理的 process tree 已空。
- 沒有 unresolved external effect。
- Call、Receipt、events 都已完整落盤。
- 沒有未發布 staging／overlay，寫入權已釋放。
- Definition 與 memory generation 已固定。

AOS 保存的是 messages、Tool Results、下一個 Step 等「邏輯續接點」，不是任意 process 的 heap、thread、fd 或 socket。

## 建議演進

1. 分出 live root、不可修改證據、本機狀態與可攜 `.aos`。
2. 做 checked-live、單一 writer、memory staging 與版本發布。
3. 以 manifest 明列 Definition inputs；自我修改發布成下一版。
4. 先 snapshot 小型 Definition 與唯讀輸入，不複製整個 root。
5. 只從 `paused-safe` 匯出固定 checkpoint。
6. 真有平行探索需求才做 branch／overlay；第一版衝突就停，不自動 merge。
7. machine identity、lease 與網路同步留到 network AOS。

## 尚待原型

- 同一 child 連讀兩次，中間換檔，live 模式是否清楚呈現 A→B。
- 在 hash 後、spawn 前精確換 A→B，證明 checked-live 的空窗。
- T1、T2 同讀 M0，T1 發布 M1 後，T2 是否 conflict 且不能覆蓋。
- 改 message 不改 Definition generation；改已宣告 helper 必須改版。
- D7 Task 產生 D8 時，目前 Receipt 是否仍只綁 D7。
- 在多檔發布各點殺 writer，是否只看見完整舊版或新版。
- 外部 counter 完成後殺 manager，是否保持一次且 Task unknown、不重跑。
- background child 未結束時，checkpoint 是否拒絕誤報 safe。
- clone 後 PID／lock／工作領取記錄是否不在 image，attach 是否零自動執行。
- 同 base 的兩個 branch 是否最多一個直接發布，另一個明確 conflict。
