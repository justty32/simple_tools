# Agent 地盤與並行候選

> 本頁整理 `agent_root_round2` 支線的想法。以下是可試作的管理方式，不是使用者已裁決的並行規格。

## 先從一個地盤開始

agent root 是一個 agent 自己管理的目錄。候選設計先加一個簡單觀念：

> 修改 agent root 的權利交給 Task 或同一棵 Task 呼叫樹，不交給某個 PID。

PID、worker claim 與 file lock 都只是某台機器當下的執行工具，不是可放進 Git 的 agent 身分。

共同底線建議如下：

- Round 呼叫同一 root 內的 Step／Tool 時，子 Task 沿用 parent 的寫入權；不能重新排隊等待自己，否則會卡死。
- 呼叫另一個 agent root 時，不沿用寫入權；另一個 agent 由自己的排程規則管理。
- 明確登記為 child agent 的子目錄，是另一塊地盤。parent 透過 Function Call 合作，不直接改 child 的檔案。
- 普通子目錄不會因為位於 `subs/` 或長得像 agent 就自動成為 agent。
- Step 的「原子」第一版只先解讀成：一個受 AOS 管理的寫入 Step 中途，不插入另一個寫入 Step。這不保證 rollback 或外部效果只發生一次。
- `pause requested` 只是請求；要等會改 root 的 process 都停下、結果落盤後，才可稱為安全暫停。

## 方案一：同一 agent 只有一棵可寫 Task 樹

一個 Round 及它呼叫的 Steps／Tools 算同一棵樹。它工作時，其他會寫同一 root 的工作排隊或收到 busy；已提交的唯讀 snapshot 可另行開放。

C++ 類比：同一個有狀態物件，一次只讓一條完整呼叫鏈修改；呼叫自己的 private method 時沿用同一份所有權。

優點：

- 最接近「同一 agent 不要同時有多個會改自己的實例」。
- 記憶的先後順序清楚，容易重現與除錯。
- pause、checkpoint 與 Git 歷史最簡單。
- crash 後只需判斷一棵活躍 Task 樹。

缺點：

- 等使用者或慢 Tool 時，其他寫入工作也得等。
- 長 Round 可能占用很久。
- paused Task 是否繼續占有 agent，仍需明定。

Child agent：parent 與 child 是不同 root，可各自有一棵可寫 Task 樹，因此仍能並行。第一版應拒絕 parent 等 child、child 又同步等 parent 的等待環。

建議：作第一版預設。

## 方案二：每個 Step 結束就可交棒

同一 root 可保存多個 Round Task，但任一時間只跑一個寫入 Step。Step 結果落盤後，另一個 Round 可以取得寫入權。

C++ 類比：多個 coroutine 共用一個物件，每次呼叫會修改狀態的方法前取得鎖，方法返回就交出。

優點：

- 某個 Round 等人或等 child 時，其他工作仍可前進。
- agent 對新工作較有反應。
- pause 自然落在 Step 邊界。

缺點：

- Round A 的兩個 Steps 之間，Round B 可能改了記憶。
- A 先前讀到的前提可能過期，行為較難重現。
- 每個 Step 都需保存與檢查 agent root 版本。

必要配套：下一個 Step 開始前比對版本；版本改變時只能明確重新讀取／重新規劃、拒絕或請人處理，不能默默覆蓋。

建議：第二階段原型，先確認版本檢查不會讓操作過於打斷。

## 方案三：每個 Task 使用自己的記憶分支

每個 Task 取得一份私有工作目錄，可各自修改 Definition 與 memory；最後以明確的「發布」動作改正式的 agent root。

C++ 類比：每個工作複製一份物件，各自修改，最後比較 base version 再決定能否換回正式物件。

優點：

- 多個方案能真正平行探索。
- 單一 Task 的暫停、保存與丟棄很自然。
- 和 Git branch 的使用經驗相近。

缺點：

- workspace、合併、版本與清理都更複雜。
- agent root 不再直接就是唯一作用中的記憶，多了一層私有視圖。
- 檔案可以丟棄，但已寄信、呼叫 API 等外部效果不能跟著 rollback。
- 不能把 child agent root 一起複製，否則會出現第二塊相同地盤。

Child agent 應視為外部 agent；parent 分支只保存對 child 的 Call 與結果引用。

建議：留給後期的研究型工作，不作第一版預設。

## 低風險折衷

在方案一內，可以允許多個唯讀 Task 讀取同一份已提交 snapshot；只有一棵 Task 樹可寫。這能讓搜尋、摘要與審查並行，不必立刻承擔 Step 交替或 branch merge。

## Pause、checkpoint 與 Git

- 方案一可先讓目前 Step 收尾，再停同一 root 的整棵 Task 樹。
- child root 還在工作時，parent 只能做局部 checkpoint，不能宣稱整隊都已停穩。
- 要做完整 team checkpoint，可先禁止建立新 child，再讓 descendants 停穩，最後保存 parent。
- Git 是保存歷史與搬運資料的工具，不是跨機器排程鎖。
- clone 後預設 detached；明確 attach 到當地 AOS 後仍保持 paused，再由使用者 resume。
- 絕對 path 可作單一 AOS 裡的 agent ID；clone 到新路徑後是新 path ID。這一輪不偷偷補 UUID。

## 建議順序

1. 方案一：同一 root 一棵可寫 Task 樹。
2. 加入不同 child roots 的並行與同-root 唯讀 snapshot。
3. 用原型試方案二的 Step 交棒和版本過期。
4. 確定真的需要同一 agent 多條可寫探索，才做方案三。

## 尚待原型

- Round 已取得寫入權後，Step child 是否會錯誤地重新排隊而卡死。
- paused Task 是保留寫入權、交出寫入權，還是由使用者選擇。
- Task A 讀完後由 B 改寫，A 能否在寫入前發現版本過期。
- branch 已造成外部效果後被丟棄，UI 是否會誤稱 rollback。
- parent checkpoint 是否錯把仍活躍的 child root 一起複製。
- Git clone 後兩台機器是否會同時自動 resume。
- parent 與 child 互相同步等待時，AOS 能否在開始前拒絕。
