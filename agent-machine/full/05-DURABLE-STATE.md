# 持久狀態：先寫穩，才往下做

AOS 把 filesystem 當成掛掉後仍在的慢速記憶體。不過「檔案存在」不等於「這件事已提交」；AOS 必須知道哪份記錄有權決定下一步、哪份只是方便閱讀的副本，以及一次寫入何時真的跨過持久屏障。

## 三個平面不要混在一起

### 中央執行權威

**目前推薦：**某台機器上的中央 AOS，才有權回答哪些工作已納管、哪些正在占用容量、誰持有寫入權、哪些工作在等條件，以及某次外部執行是否已有持久意圖或可信結果。active set、claim、queue、attempt 與本次 AOS 啟動代號都屬 machine-local（本機）執行狀態。

排程與 dispatch 每次都要查這份權威。關係也必須由明確記錄建立；不能掃到某個 Task 目錄，就猜它是 root、child 或可執行工作。

### agent root 內的本機顯示

**使用者已定：**同一 agent root 若有多個 active Tasks，root 內必須看得見，而且由 AOS 管理。

**目前推薦：**這只是中央狀態投出的可重建顯示，供正在該地盤工作的 agent 閱讀。它不負責搶容量或批准 dispatch，不進 Git，也不參與 Definition 或 Memory 的內容版本。AOS 離線時，顯示必須明說它是上次快照；刪除或修改它，也不能改變中央 claim。

### 可攜 `.aos`／Git 狀態

這一層只保存已封存的語意狀態，不能帶走「原機仍在跑」的假象。它不包含 PID、lock、worker claim、active set 或本次啟動代號。只有穩定 checkpoint 才能成為可攜狀態；細節見 [07](07-CHECKPOINT-GIT-AND-PORTABILITY.md)。`.aos` 的精確布局與哪些檔案進 Git 仍未決定。

## 持久屏障

**目前推薦：**每個會改變後續判斷的動作，都採同一原則：先把足以恢復判斷的資料完整寫入、驗證並提交，才可對外宣稱完成或造成下一個作用。

由小到大有幾個關鍵邊界：

1. AOS 對外確認工作已納管前，接受依據必須先持久化；若寫入失敗，就不能回報已接受。
2. parent／child 或 root 關係先由權威提交，才可把對應工作視為可達或可執行；目錄本身不是關係。
3. 可能造成 process 啟動、寄信、HTTP 呼叫或其他外部作用前，先持久提交 dispatch intent（即將執行的意圖）。
4. 完整結果與其執行依據先發布並驗證，之後才提交「結果已完成」；parent 也只能觀察已提交且可驗證的 child 結果。
5. 任一 I/O 在屏障前失敗，就停在屏障前，不得用記憶中的狀態越過它。

普通檔案常用「同目錄暫存檔、完整寫入、同步檔案、原子改名、同步目錄」形成一個發布點；append log 也要提交完整記錄。這是目前安全實作方向，不是跨檔案 transaction，更不自動保證斷電、NFS 或裝置快取下的語意。

## 結果不明不是失敗，也不是重試許可

**目前推薦：**已有持久 intent，卻缺少可信的完整結果時，Task 必須停在 **unknown（結果不明）**。它表示外部作用可能已發生，只是 AOS 無法證明結果。

- 不自動再送一次，不因重開就建立新 attempt。
- 不把部分 stdout、殘留檔案或「process 已不見」猜成成功或失敗。
- 不捏造 Function Return 或完成證明；P1 工作台暫稱後者為 Receipt。
- 修復、向具防重複能力的外部服務查詢，或明確重做，都是另一次有意識的操作；服務提供的防重複 key 不能外推成 AOS 的 exactly-once 保證。

若完整、不可矛盾的執行證據已在，只差一份可唯一推導的索引或完成標記，recovery 可以補回同一份記錄。只要仍有兩種可能，就維持 unknown 或停止待修，不替使用者猜。

## 原型證據到哪裡

**原型暫選：**P1 工作台用不可變 Call、Task-local Call、ordered events、Receipt 與 single writer 驗持久順序；它的檔名、JSON、hash、ID、容量與 `<store>` 都不是正式 AOS schema。

目前測試只支持同一 Linux filesystem 上的 process-kill／replay 小切片：已知結果可補齊投影，結果不明不重做，兩個 fake child 可在中斷後回到相同持久 bytes。它沒有證明 power-loss durability、多 writer、NFS、portable checkpoint、完整 AOS 生命週期或 exactly-once。AOS 自己如何重開，接著見 [06](06-AOS-LIFECYCLE-AND-RECOVERY.md)。
