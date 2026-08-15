# AOS 自己的生命週期與恢復

本頁說的是 **AOS 本身**何時可接工作、何時只能恢復、何時正在排空；不是 Task 的正式狀態表。使用者已定 AOS 必須管理自己的生命週期，但常駐或按需啟動、內部分成幾個 process，以及正式狀態名稱都尚未決定。

## 一次安全的啟動

**目前推薦：**AOS 由小到大經過以下階段：

1. **停止／不可用**：沒有 Runtime 可以代表中央執行權威。
2. **啟動與恢復**：取得中央 store 的排他寫入資格，建立新的本機啟動代號，確認舊 writer 與受管理 process 不再寫入，再驗證持久狀態。此時不接受新工作，也不 dispatch。
3. **ready**：恢復已到穩定點，權威索引與 root 內顯示已重建，才開始接受、排程及執行。
4. **drain**：停止接收新工作與新 claim，讓已納管工作走到可安全停止、已知完成或結果不明的邊界。
5. **停止**：持久記錄已提交、可停止的 worker 已停、中央寫入資格已釋放。

crash 可以發生在任何階段。下一次啟動不能沿用上次記憶中的 ready，也不能只因 PID 已消失就猜工作沒做過；它必須重新走恢復。上述階段是安全骨架，不是已凍結的 enum 或公開介面。

## recover 完，才可以 ready

恢復的目的不是「盡量把所有東西跑完」，而是先找出唯一可信的現在。

**目前推薦的次序：**

1. 從中央明確的 registry、已提交關係與內容引用開始，純讀驗證可達狀態；不得掃目錄猜 root、child 或待執行工作。
2. 驗證已提交記錄的順序、引用與內容彼此一致。只有可辨識的最後殘尾，才可安全移除或補齊。
3. 若完整既有證據能唯一決定下一筆無副作用記錄，一次只補一個缺口；每寫一步便重新全驗。
4. 已有 dispatch intent 而結果證據不完整時，標為 unknown，不啟動舊工作、不重送外部作用，也不接回一個猜測中的 PID。
5. 出現相互矛盾的已提交資料、越權下游資料或無法證明舊 writer 已停時，fail closed：保持非 ready，留下診斷，等待明確修復。
6. 全部到達穩定投影後，才重建查詢索引與 agent root 內的本機顯示，最後進入 ready。

recovery 不應呼叫用來接受新工作的 resolver，不讀臨時命令列或 ambient 設定重建舊請求，也不自行清理未被權威引用的孤兒資料。清理與修復是有明確範圍的維護操作，不是恢復時順手猜測。

## 新啟動世代要隔離舊工作

本機啟動代號每次 restart 都要更換；它不是可攜內容版本。舊 worker、舊 claim 或晚到的結果不能只憑 Task ID 就跨世代提交。

取得中央 store 的鎖，也不等於舊 process tree 已停止。進入 ready 前，AOS 必須證明舊 writer 與仍可修改正式 root 的受管理 process 已終止或被可靠隔離。無法證明時就不能派新工作；若舊作用可能已發生而結果未留下，仍按 unknown 處理。

agent root 內的 active 顯示可帶本次啟動代號與更新序號，幫助讀者辨識新舊；但它仍只是投影。AOS 連不上時只能標成過期，不能接管中央權威。

## drain 不等於 checkpoint

drain 只表示 AOS 不再擴大工作集合。某個 Task 可能已完成、停在安全續接點、仍等待受管理 process，或因外部作用不明而 blocked。AOS 不可為了順利關機，把最後兩種狀況改寫成成功或可攜 checkpoint。

乾淨停止應盡量讓工作到達可說明的邊界、提交中央 machine-local 記錄並釋放寫入權；crash 則沒有這個保證。兩者下一次都要 recover-before-ready。只有符合 [07](07-CHECKPOINT-GIT-AND-PORTABILITY.md) 的 paused-safe 穩態，才可另外封存成能移動的 checkpoint。

## 已驗與未驗

**原型暫選：**P1a-2 工作台的 `recover_store()` 採 single writer、明確 relation authority、一次補一個缺口及每步重驗。除了 fake 串接，第一個真 process 窄切片也已驗到：完整 raw 只補提交、after-spawn 缺完整結果時不重跑，以及已凍結 recipe／外部作用屏障／矛盾資料的保守停止。精確範圍見[證據頁](../wait_user/2026-08-14-deep-dive-04-evidence.md)。

這仍只是一條 store recovery 入口與受控 process seam，不是完整 Phase 2，也不是完整的 start／ready／drain／stop Runtime。完整 process hooks／root 中斷矩陣、舊 worker fencing、掉電、多 writer、NFS 與跨機仍未驗；正式 store 型態、鎖法、啟動代號格式及 Runtime process 拆分也未決定。
