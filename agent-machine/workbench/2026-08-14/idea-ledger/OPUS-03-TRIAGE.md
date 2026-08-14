# Opus 03 取捨紀錄

> 外部審查的取捨紀錄，不是正式規格；使用者後來的決定優先。

## 最重要的覆蓋

Opus 03 原本建議：同一 agent 忙碌時，普通新工作預設建立 Task 並排隊。

這一點已被使用者後來的明確決定覆蓋，不採用。現在固定為三條不同操作：

1. **普通新呼叫**：agent 忙碌就拒絕並告知，不建立 Task。
2. **明確追加 prompt**：加入目前 Task，不建立另一個 Task；在哪個 Step／Round 邊界生效仍待原型。
3. **明確建立排隊 Task**：建立新 Task，並且必須寫出「目前 Task 到了什麼狀況才可執行」。

因此「拒絕」不會偷偷保存工作；只有使用者明確選排隊，AOS 才建立排隊 Task。

## 採用的 Task 延伸

使用者原本只說「正在執行中的 Function 實例叫 Task」。為了讓 AOS 能排隊、暫停與重啟恢復，本輪採用以下延伸：

- Call 通過驗證並安全落盤後，就取得固定 `task_id`。
- 同一 ID 貫穿排隊、執行、等待、暫停、阻塞與完成後紀錄。
- 每次啟動 Linux process 都是 Task 內部 attempt；PID 不是 Task ID。
- 可能已有外部作用但結果不明時，不因「可新增 attempt」就自動重做。

這是 AOS 工作模型的採用項，不冒充成使用者原句。

## 同一 root 的寫入權

第一版採「同一 agent root 只有一棵可寫 Task 呼叫樹」：

- Parent Task 呼叫同一 root 內的 child Task 時，child 沿用同一份寫入權，不重新排隊等待自己。
- 一棵呼叫樹可同時出現多個 Task ID，但占用的 root writer slot 仍是 1。
- 呼叫另一個 child agent root 時不傳寫入權；另一個 root 由自己的排程管理。
- active Task 數量不等於 writer slot 數量。

這能避免 parent 占著 root、child 又等 root 的自我卡死。

## 啟動前的兩道關卡

條件式 Task 不能因前一個 Task 狀態符合，就直接啟動：

```text
第一道：條件關卡
  前一個 Task 是否到了指定狀況？
  -> 通過後只表示「可以競爭執行」

第二道：root 容量／寫入權關卡
  目前 root 是否容許這棵 Task tree 加入？
  -> 中央 AOS 原子建立 claim

安全落盤 dispatch intent
  -> 完成後才可跨進 process／外部作用
```

如果第二道關卡沒拿到容量，Task 保持「條件已滿足、等待容量」，不回頭重算條件，也不啟動。

`dispatch intent` 是兩道關卡後的必要落盤屏障，不是第三種排程條件。intent 尚未落盤就不得造成外部作用；intent 已有而結果證據不完整時，先按結果不明處理。

## 中央狀態與資料夾內可見資訊

使用者要求：同一 root 若同時執行多個 Tasks，資料夾內的 agent 必須看得見目前 active Tasks，而且由 AOS 管理。

採用方式是：

- **中央 AOS 是唯一真相**：active set、capacity、claim 與 recovery 狀態都在中央確認。
- **資料夾內是可重建的顯示副本**：方便 agent 閱讀，但不能靠它搶容量或決定 dispatch。
- 顯示副本帶本機 AOS 啟動代號與更新序號；AOS 連不上時要明說「只剩上次狀態」，不能冒充即時資料。
- 任何會啟動、停止或發布工作的操作，都必須再向中央 AOS 確認。
- 資料夾內狀態是 machine-local，不進 Git，也不改變 Definition 或 memory 的內容版本。

即使 Task 能刪掉或改名該顯示檔，中央 claim 也不受影響。

## 並行演進順序

### 1. 完全序列化

同一 root 一次只有一棵可寫 Task tree。這是第一版。

### 2. 唯讀 Snapshot

允許其他 Task 並行讀取已發布、不可修改的 snapshot；它們不直接取得 live root 的普通寫入能力。正式 root 仍只有一個 writer。

### 3. 私有工作區

真的需要多個可寫探索時，每個 Task 使用私有 workspace：

- 每個 workspace 記住開始時的 memory 版本。
- 執行可以並行，但發布回正式 root 仍一次一個。
- 發布時 base version 已改變就明確衝突，不採最後寫入者覆蓋。
- workspace 不是新 agent ID，也不能把外部 API／寄信效果一起 rollback。

不採用「多個 writers 直接共寫同一個 live root」。看得見彼此並不能阻止舊讀取、覆蓋、Definition 半途改變或 Git 操作衝突。

## 三種版本不要混名

採用兩個內容版本，加一個本機啟動代號：

- **Definition version**：這次 Function 使用哪版程式與已宣告依賴。
- **Memory version**：agent 已發布到哪版記憶。
- **Runtime startup token**：目前是哪次本機 AOS 啟動；每次 restart 都換，不是可攜內容版本。

資料夾內 active 狀態變動，不得讓 Definition 或 Memory version 跟著改變。

## 不弱化 paused-safe

排隊、active 顯示或私有 workspace 都不能把普通 pause 假裝成安全存檔點。`paused-safe` 仍至少要求：

- 沒有仍可修改正式 root 的 process。
- 受 AOS 管理的 process tree 已停穩。
- Call、Receipt 與 events 已完整落盤。
- 沒有未處理的外部作用結果不明。
- 沒有未發布 staging／workspace，或它們已明確封存成只能診斷的資料。
- root 寫入權已釋放，Definition 與 Memory version 已固定。

不滿足時只能標成「要求暫停中」或「僅供診斷」，不能匯出成可自動續接的 checkpoint。

## 六步原型順序

1. **忙碌三路**：證明普通呼叫被拒絕且 Task 數不變；追加不產生新 ID；明確排隊才建立固定 ID 與條件。
2. **同 root 呼叫樹共用寫入權**：parent／child 同時可見但 writer slot 仍為 1，且不會互等卡死。
3. **兩道關卡加 intent**：條件已滿足但容量已滿時啟動數為 0；兩個 scheduler 搶最後一格只能一個成功；intent 落盤前 effect 為 0。
4. **中央狀態投到資料夾**：故意讓顯示副本和中央矛盾，所有排程仍以中央為準；AOS 離線時只顯示過期警告。
5. **唯讀 snapshot 並行**：多個讀者可同時工作，卻不能修改 live root；Definition／Memory version 與 active 顯示彼此分開。
6. **私有 workspace 發布**：兩個 Task 從同一 base 修改同名檔，只能第一個發布；第二個明確衝突。最後重跑 `paused-safe` 與外部作用結果不明測試。

這六步依序通過後，才討論提高同一 root 的可寫並行度。
