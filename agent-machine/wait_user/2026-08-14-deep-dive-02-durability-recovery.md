# 持久化與恢復：先寫穩，才往下做

這是 [`full/05–07`](../full/05-DURABLE-STATE.md) 的濃縮導讀。重點不是某個 JSON schema，而是 crash 後仍能唯一判斷下一步的提交順序。

## 三種資料地位

- **Authority（權威）**：唯一能決定「已接受、可執行、已完成」的持久記錄。
- **Staging（暫存）**：正在發布的資料；檔案存在不代表已提交。
- **Projection（投影）**：由權威重建的人類報表、root 內 active 顯示或索引；不能反過來授權 dispatch。

**目前推薦：**某台機器的中央 AOS 掌握 queue、claim、active set、寫入權與執行證據。Agent root 內的 active 顯示只是可重建的本機投影，不進 Git；可攜 checkpoint 又是另一份只含穩定語意的資料。

## 外部作用前的屏障

把寄信、呼叫 API 或啟動 process 當成不可隨意重做的 effect（外部作用）：

```text
驗證 Call
  -> 持久接受
  -> 持久 relation / claim
  -> 持久 dispatch intent
  -> 才能造成外部作用
  -> 保存完整原始證據
  -> 驗證證據並提交 Receipt
  -> 由證據支持已知 Return（兩者不是同一物件）
  -> parent 才能觀察完成
```

檔案層常見做法是：同目錄暫存檔 → 寫完 → `fsync(file)` → atomic rename → `fsync(directory)`；journal 則一次追加完整 record 再 `fsync`。這是目前推薦的順序，不是承諾所有檔案系統都等價。

## Recovery 不是「把缺的都補上」

P1 工作台採用的保守演算法是：

1. 先取得單一寫入權。測試驅動器會先等待舊 writer process 結束；正式 Runtime 如何隔離仍活著的舊 worker 尚未驗證。
2. 純讀驗證 registry、明確 relation、所有可達 Task 與已提交證據。
3. 只修可唯一辨認的最後 torn tail（未完整追加的尾端），修完重新全驗。
4. 找出唯一合法缺口；一次最多補一個狀態轉移。
5. 每次寫入後從頭重驗；遇到矛盾立即停止。

它不掃目錄猜 root／child、不讀當下環境重建已接受的 Call、不用後續資料把早期越權寫入「洗成合法」，也不因結果缺失就重跑。

## 已知失敗與 unknown 要分開

- `exited(nonzero)`、signal 或可辨識的 launch error，可以是**完整已知**的 leaf Return；parent 再決定業務上如何處理。
- 已有 dispatch intent，但沒有可信完整結果，就是 **unknown（結果不明）**。外部作用可能已發生，所以沒有 Receipt，也不能自動重送。
- 若原始證據完整，只差可唯一推導的索引或投影，recovery 可以補齊；若仍有兩種可能，必須停住等待 repair（修復）或明確建立新工作。

## 重開與搬家是兩件事

**使用者已定：**AOS 要管理自己的重開；可攜語意與某台機器的執行狀態要分開。

**目前推薦：**Runtime 啟動先 recover，完成前不 dispatch；drain 後不再接受新工作。`paused-safe` 是「可安全封存的暫停點」：沒有仍可寫 root 的 process、沒有結果不明的外部作用或未發布 staging，Definition／memory 世代固定，且下一個續接位置明確。可攜 `.aos` checkpoint 才可收這個穩態；除了 messages、已知結果與必要關係，還要保存下一個邏輯續接位置，以及可驗證 Definition／memory 世代的資訊，不能 attach 時從環境猜回。它排除 PID、FD、lock、claim、active set、進行中 Attempt 與未完成暫存。Clone 到新機先 detached／paused，不能自動續跑。

常駐或按需 Runtime、正式狀態名、`.aos` 版面與資料庫都尚未決定。

## 證據停止線

目前只驗同一 Linux 檔案系統上的 process-kill／故障切點模型。它不等於斷電、裝置快取、NFS、多寫入者、TOCTOU、恰好一次或外部作用回滾。

完整原文：[`full/05`](../full/05-DURABLE-STATE.md)、[`06`](../full/06-AOS-LIFECYCLE-AND-RECOVERY.md)、[`07`](../full/07-CHECKPOINT-GIT-AND-PORTABILITY.md)。精確工作台規則見 [P1a-2 authority](../workbench/2026-08-14/round-2-decision/06-FUNCTION-TASK-MODEL.md)。
