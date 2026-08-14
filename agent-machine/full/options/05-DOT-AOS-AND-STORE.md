# `.aos` 與中央 store 的分工選項

狀態：**尚未決定**。使用者偏好 `.aos`，也希望 Agent root 可進 Git；但這不代表 queue、claim、PID 或目前正在執行的工作應跟著 repo 搬走。主線只固定「可攜語意」與「本機執行權」必須分開，精確目錄與資料庫格式仍可更換。

共同底線見[持久狀態](../05-DURABLE-STATE.md)與[checkpoint／Git](../07-CHECKPOINT-GIT-AND-PORTABILITY.md)：中央 AOS 是本機 active set、claim、attempt 與 recovery 的真相；`.aos` 最多攜帶已封存且可驗證的穩態，不能讓新機誤以為舊 PID 還活著。

## 方案一：所有資料都放 Agent root

概念上在 `.aos/` 內再分可攜區與被 Git ignore 的本機區；中央 AOS 只保存最小索引。

- **優點**：最貼近「資料夾就是地盤」；人可直接閱讀；Python 原型容易做。
- **缺點**：兩種壽命的資料靠得太近，容易誤 commit；跨 root 的 queue 與 claim 難做原子更新；中央查詢可能要走訪許多 repo。

若 verifier 不能可靠拒絕被誤放進 checkpoint 的 claim、PID、lock 與 active 顯示，本案不安全。

## 方案二：中央執行 store，加 repo 內 `.aos` checkpoint

中央 store 保存 machine-local 執行權威；`.aos` 只保存已提交的 checkpoint、必要 provenance（來源說明）與可攜內容。root 內若要顯示 active Tasks，另放可重建且被 Git ignore 的本機投影。

- **優點**：執行權與可攜歷史最清楚；Git 不會帶走「原機仍在跑」的假象；中央可原子管理多個 roots。
- **缺點**：中央狀態通常比上次 checkpoint 新；兩邊發布順序與損壞修復必須明定；使用者要理解 live 與 checkpoint 的差別。

中央 store 可先用普通檔案與 journal，之後才以同一批測試比較 SQLite；選本案不等於現在決定資料庫。

## 方案三：`.aos` 只保存中央引用

repo 只留一個小 manifest，內容本體全在中央 store。

- **優點**：repo 很小；不會複製大量執行證據；中央清理容易統一。
- **缺點**：clone 後缺中央物件就無法獨立閱讀，不符合可攜目標；引用失效時 checkpoint 只剩空殼。

這適合作本機捷徑，不適合作唯一的可攜形式。

## 方案四：不可修改的內容物件包

Call、Return、checkpoint 與 event segment 依內容識別保存；`.aos` manifest 列出需要的物件，也可把物件一併打包。

- **優點**：audit、fork、去重與未來網路同步最好；已發布 checkpoint 天生不原地修改。
- **缺點**：缺件、權限、格式升級與垃圾清理會把近期工作變成物件庫專案；人類閱讀較差。

這是遠期候選，不應先於基本 checkpoint 成立。

## 目前推薦

採用**方案二的責任分工**，但不凍結檔名或 SQLite schema：中央 store 掌握 live authority；repo 內 `.aos/` 保存完整、不可修改的可攜 checkpoint；本機 active 投影與 workspace path 明確排除於 Git。checkpoint attach 到另一台機器後先 detached／paused，零自動 dispatch，使用者明確 resume 才執行。

這尊重 `.aos` 偏好，也避免把它變成第二份 scheduler 真相。P1 工作台的 `<store>` 只證明 single-writer 保存切片，不是 `.aos` 版面或 portable image。

## 驗證與推翻條件

1. exporter 應能保存 messages、已知 Returns 與關係，同時拒絕 PID、FD、lock、claim、active set 與未完成 attempt。
2. 在 staging、發布、中央登記與可攜指標更新各點殺掉 writer，只能看見完整舊版、完整新版或明確停止。
3. clone 到新路徑後，查詢可讀但啟動次數為零；attach 後仍 paused，Definition 不符時拒絕 resume。
4. root 內投影故意與中央矛盾時，所有 dispatch 仍以中央為準；AOS 離線只能顯示 cached／過期。
5. 普通檔案與 SQLite 若跑同一 crash corpus 得到不同恢復判定，先停下修語意，不提升任一格式。

若方案二無法在 crash 後判定中央記錄與 `.aos` checkpoint 是否成對，或日常備份必須依賴不可取得的中央 store，便推翻目前推薦，退回方案一的明確分區再比較。更多原始候選見[持久化候選](../../workbench/2026-08-14/idea-ledger/DURABILITY-CANDIDATES.md)。
