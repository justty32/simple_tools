# Task 生命週期候選

> 本頁整理 `task_lifecycle_round2` 支線。使用者只明確說「正在執行中的 Function 實例叫 Task」；以下是在可排程、可重啟的 AOS 裡延伸這個詞的候選。

## 先分清四件事

- **Definition**：Function 的定義，位於明確 filesystem path。
- **Call**：一次呼叫請求，例如 Definition、argv 與 stdin。
- **Task**：AOS 管理這次工作的身分與生命週期。
- **Attempt**：Task 某一次實際啟動 process 的內部執行次數。

爭點是：Task 從何時開始、何時結束，以及 process crash 後 Task ID 是否改變。

## 方案一：Task 隨 PID 生滅

只有 process 正在跑時才有 Task；process 結束，Task 也消失。

優點：最貼近 Linux，概念少；早期一次性 runner 容易做。

缺點：queued、paused 與等待 child 時沒有穩定身分；AOS crash 後很難說同一工作；PID 可重用也不可跨機；完成結果沒有自然的查詢位置。

建議：只作 Linux process 層的內部觀察，不作 AOS 公開模型。

## 方案二：排隊時是 Call，開始執行才升成 Task

接受後先回 Call ID；真正 dispatch 時再建立 Task ID。

優點：嚴格保留「正在執行的實例才叫 Task」；尚未執行的請求與執行個體分得很乾淨。

缺點：使用者先拿 Call ID，之後又換 Task ID；parent-child relation、pause、取消與 CLI 都要處理升格；crash 正好發生在升格點時，需維護兩套連結。

適合：非常重視形式區分、願意承擔雙 ID 的系統。

## 方案三：安全落盤接受後固定 Task ID

Call 通過驗證並安全落盤（durable accept）後，就取得一個固定 `task_id`。同一 ID 經過排隊、執行、等待、暫停、阻塞到結束；狀態名稱仍可調整。

優點：

- 使用者從接受、等待、暫停、重啟到查完成結果都只用一個 ID。
- parent-child relation 不需在 dispatch 時換身分。
- AOS crash 前尚未 dispatch 的工作也能用同一 ID 恢復。
- Task 目錄與事件記錄不因 process 結束而搬家。

缺點：

- 比使用者原句更廣：queued 與已完成的保存記錄也沿用 Task 一詞。
- 必須說清楚「正在執行」是 Task 的一個狀態，不是 Task 的全部生命期。
- 完成後保存多久與何時清理仍待決定。

建議：作公開模型的第一選擇，但文件要誠實標成 AOS 的延伸定義。

## 方案四：固定 Task ID，內部再有 Attempts

在方案三之下，每次真正 dispatch 都建立內部 attempt。Task ID 不變，PID、worker、啟動世代與 process evidence 歸 attempt。

```text
Task T42
  attempt-1 -> process P100 -> crash／unknown
  attempt-2 -> 只有明確政策允許時才建立
```

優點：Task 的邏輯身分不綁 PID；能表達啟動失敗、manager crash 與有條件重試；診斷資料更清楚。

缺點：內部格式更複雜；若 UI 暴露太多 attempt，認知負擔會上升；有人可能誤以為「有 attempt 就能安全重做外部作用」。

建議：公開 UX 採方案三，Runtime 內部採方案四；一般操作只顯示 Task ID，診斷時才顯示 attempt。

## 接受與結束的邊界候選

```text
解析／驗證 Call
  -> 失敗：回報錯誤，不建立 Task
  -> 安全落盤接受：建立固定 task_id
  -> queued／running／waiting／paused／blocked
  -> 結束狀態
  -> 保留為「已完成 Task 的紀錄」
```

- 接受記錄必須真的落盤後才對命令列端回成功。
- 結束後 ID 與目錄不換；Receipt 仍由原 Task ID 查詢。
- process 結束只是 attempt 結束，不必等同整個 composite Task 結束。
- AOS 重啟後先重建 Task 狀態，再決定能否建立新 attempt。
- 結果不明（unknown）不等於允許重試；可能已有外部作用時不得自動重送。

「完成後仍叫 Task」容易讓字面不自然。對使用者可說「已完成 Task 的紀錄」，不用另造第二個公開 ID 或搬移目錄。

## CLI 候選

```sh
aos exec -- ./ask ...       # 安全落盤接受；預設可順便等待
aos task wait TASK_ID       # 連回既有 Task，不建立新的
aos task list               # 預設列未完成
aos task list --finished    # 明確列已完成記錄
```

Ctrl-C 只離開等待畫面；Task 是否停止要用另一個明確操作。詳細操作面見 [`CLI-CANDIDATES.md`](CLI-CANDIDATES.md)。

## Parent、Child 與 Pause

- parent planned child 時，就保存 child Task ID；child 從排隊到完成不換 ID。
- parent 等 child 時仍是同一 parent Task，只是進入 waiting 狀態。
- pause／resume 不建立新 Task；若 Runtime 需要重新啟動 process，差異放在 attempt。
- child Receipt 被 parent 觀察後，兩邊 ID 與 relation 都不重寫。

## Retry 的安全邊界

Attempt 是記錄工具，不是自動重試許可。

- dispatch 前失敗，且能證明沒有外部作用，才可能安全建立新 attempt。
- dispatch 後缺完整結果，先標 unknown；除非 Function 或外部服務提供可驗證的重試規則。
- 使用者明確選擇「再做一次」時，也可建立新 Task，而不是偷偷改寫原 Task 的歷史。

## 建議順序

1. 原型比較方案二與三，量出雙 ID 對 CLI、relation 與 crash recovery 的成本。
2. 第一版公開採方案三：安全落盤接受後固定 task_id。
3. Runtime 內加入方案四的 attempt，但一般 UX 隱藏。
4. 完成後沿用同一 Task ID 與目錄，只調整 list 的預設篩選。
5. 最後才決定清理、壓縮與長期保存政策。

## 尚待原型

- durable accept 後、dispatch 前殺掉 manager；重啟是否仍是同一 ID，effect 次數為 0。
- parent 建 child、child queued／running／terminal 全程，relation 是否不換 ID。
- running→pause→restart→resume 是否保持 Task ID，只新增必要 attempt。
- terminal 後原 ID 是否仍可查 Call、Receipt 與 parent-child relation。
- Ctrl-C 後 `task wait` 是否只重連，不建立第二個 Task。
- 方案二與三完成同一 sequence 時，各需幾個 ID、CLI 分支與 relation 更新。
- dispatch 後結果不明時，是否停在原 Task 的 blocked／unknown，而不自動建 attempt-2。
