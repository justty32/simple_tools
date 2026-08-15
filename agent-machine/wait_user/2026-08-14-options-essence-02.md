# AOS 選項精華（二）：排程器、busy、CLI 與語言邊界

這是 [`full/options/06–09`](../full/options/06-SCHEDULER-SHAPE.md) 的決策地圖。Busy 的三條入口已由使用者決定；本頁其餘架構與介面仍是推薦或待驗方案。

## 本頁短詞彙

- **Authority（權威）**：唯一能授權狀態轉移的持久記錄；顯示畫面不是權威。
- **Active set**：目前占用執行或 root 寫入資格的工作集合。
- **Claim**：排程器在同一原子判定中取得的執行資格。
- **Event loop／executor**：前者仲裁狀態，後者實際執行；`seam` 指兩者可測試的接縫。
- **Crash corpus**：固定的一組中斷位置與恢復反例。
- **Startup token**：Runtime 本次啟動世代的識別值，用來拒絕舊 worker 的遲到結果。
- **Proposal**：Janet 提出的純資料建議；C++ 重驗前沒有寫入權。

## 06｜Scheduler 的形狀

**決定什麼：**中央 Runtime 是按需還是常駐、executor 是否另拆 process，以及誰原子管理 active set、root claim 與容量。

**狀態：**中央仲裁與 effect 前 intent 是**目前推薦**的安全線；按需單 process 是**目前推薦的下一個原型方案**；常駐、拆 executor 或每 Agent worker 都**尚未決定**。

**目前推薦：**先用「按需單 process」實作「中央 Runtime」的語意：單一 authority、單一 event loop、明確 executor seam。Crash corpus 穩定後，再決定是否常駐或拆 executor；不先固定 socket、queue JSON 或 SQLite。

**代價：**按需版簡單但無人操作時不前進；常駐版能跑背景 queue，卻增加啟停、升級與故障範圍；每 Agent worker 又會形成更複雜的分散式協定。

**推翻證據：**兩個 scheduler 搶最後 slot 只能一個成功；recovering 時零 dispatch；舊 startup token 結果被拒；event loop 若無法同時排空 leaf streams 且維持控制面回應，再拆 executor。

**使用者裁決：**目前不需要；部署方式開始影響常駐背景行為或營運介面時才需要。

原文：[`options/06`](../full/options/06-SCHEDULER-SHAPE.md)。

## 07｜Agent busy 與 Step 邊界

**決定什麼：**不是重議 busy 三路，而是追加何時生效、queue condition 長什麼樣、paused 是否交出地盤。

**使用者已定：**普通 busy 呼叫拒絕且零 Task；明確追加留在目前 Task；明確排隊才建新 Task，並指定開始條件。

**狀態：**上面三路是**使用者已定**；下一 Step 才採用、同 root 單一可寫 Task tree 是**目前推薦的下一個原型方案**；paused 是否交出地盤**尚未決定**。

**目前推薦：**追加先持久保存，下一個 Step Return 完整提交後恰好消費一次；unknown 時保留 pending 並 blocked。同 root 只有一棵可寫 Task tree，children 繼承 parent 資格。

**代價：**Step 邊界最容易重播，但長 Step 可能讓補充久等；Round 結束更穩定卻更慢；paused-only 可能永久等候。中途注入會讓已 dispatch 的輸入改義。

**推翻證據：**Busy 拒絕前後 Task／queue 不變；追加跨重開只消費一次；條件已滿但 root 忙時啟動數仍為 0；parent／child 不形成自鎖。

**使用者裁決：**長 Step 的追加延遲若不可接受，或要讓 paused 交出地盤時，需裁決可見邊界與風險。

原文：[`options/07`](../full/options/07-AGENT-BUSY-AND-STEP-BOUNDARY.md)。

## 08｜CLI 表面

**決定什麼：**完全明示、root 內相對呼叫、PATH proxy 與 nested shell 怎麼分工；命令名稱、exit code 與 machine output 都未固定。

**狀態：**偏好 shell／PATH 是**使用者已定**；明示入口是**目前推薦**的測試基準；正式命令與 nested shell **尚未決定**。

**目前推薦：**完全明示入口作測試語意基準，再讓 `cd bot-a` 後的相對呼叫共用同一實作。PATH 第一版只放薄控制代理；nested shell 等 wait／detach 與 root attach 穩定後再試。

**代價：**明示入口安全但冗長；相對入口舒服卻需要可信 root context；PATH 可能殘留舊 root 或長出第二套 parser；nested shell 帶來 TTY、Ctrl-C 與環境還原問題。

**推翻證據：**Parser 覆蓋 `--`、空白、dash 開頭值與 raw stdin；foreground、detach、Ctrl-C、重新等待全程只有一個 Task／effect；兩個 roots 的同名代理不能送錯。

**使用者裁決：**目前不需要；先量 2–5 分鐘日常流程，正式固定命令拼法與 Ctrl-C 語意時再選。

原文：[`options/08`](../full/options/08-CLI-SURFACE.md)。

## 09｜C++／Janet bridge

**決定什麼：**C++、Python、Janet 各擁有哪段權威，以及第一個跨語言接法是兩個 process、內嵌 VM、native module 還是 FFI。

**狀態：**Python 探索、C++ 固化硬邊界是**目前推薦**；Janet process bridge 是**目前推薦的下一個原型方案**；正式語言與 ABI 分工**尚未決定**。

**目前推薦：**Python 先找狀態機反例；C++ 守住 process、FD、lock、`fsync`、證據重驗與正式 commit；Janet 只讀已驗證、與 live store 脫離的 snapshot，回傳 proposal。第一個 Janet 整合先用兩個 process 傳有界資料；C++ 收到後重新驗證才提交。

**代價：**Process bridge 隔離好但有啟動／編解碼成本；內嵌 VM 快但與 Runtime 共用 crash 範圍；native module 容易讓寫入權滑到 Janet；FFI 有 pointer／allocator／ABI 風險。

**推翻證據：**先通過 Linux Janet toolchain gate；Janet timeout、crash、壞輸出、偽 Receipt 與過期 reference 都要保持 store 零寫入。只有 process overhead 成為實測瓶頸，且 embed 通過 thread／fork／VM 重建測試，才縮短隔離邊界。

**使用者裁決：**目前不需要；Linux gate 與實測成本完成後，要固定正式語言邊界時再選。

原文：[`options/09`](../full/options/09-CPP-JANET-BRIDGE.md)。

## 現在要不要決定？

| 題目 | 現在的處理 |
|---|---|
| Function 入口 | **不必定案**；先驗固定 `call`，不夠再升 manifest。 |
| Task 詞義 | 公開介面前需裁決：只指 running，或接受後一路沿用。 |
| Memory 模式 | **不必定案**；依資料類型做最小反例。 |
| Return／repair ABI | **不必定案**；先守住 unknown 不自動重送。 |
| `.aos` checkpoint | portable 格式前需裁決自足程度與人類可讀性。 |
| Scheduler 部署形狀 | **不必定案**；先固定中央 authority 語意與 crash corpus。 |
| Busy 三路 | **已定**；Step 延遲或 paused 交地盤不可接受時再裁決邊界。 |
| CLI 拼法 | **不必定案**；先有明示基準，再做短 UX 試用。 |
| C++／Janet 接法 | **不必定案**；先完成 C++ durable seam 與 Linux Janet gate。 |

裁決原則是：近期實作真的被阻塞，且小原型已排除較小方案，才固定更厚的介面。測試通過也只提升已驗邊界，不自動固定 schema、命令字串或部署拓撲。

上一頁：[Function、Task、memory 與保存](2026-08-14-options-essence-01.md)。共同狀態邊界見 [`full/00`](../full/00-STATUS-AND-SOURCES.md)。
