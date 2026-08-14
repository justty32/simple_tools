# 限制與以後再做

本頁集中列出近期不承諾的能力。把限制寫在這裡，是為了避免其他章節用一句「之後可支援」偷偷擴大第一版。

## 第一版刻意保持小

**目前推薦**：第一版先證明一台 Linux 機器、普通本機檔案系統、單一 AOS 寫入權威，以及每個 Agent root 一棵可寫 Task tree。它要能：

- 接受明確 Function 呼叫並建立可追蹤的執行實例。
- 在外部作用前保存意圖，在可信結果後保存證據。
- AOS 重開後保守恢復，不盲目重送結果不明的工作。
- 分開中央執行狀態、root 內本機顯示與可攜 checkpoint。
- 讓 Agent 層在同一基礎上建立 Step、Tool 與 Round。

這個範圍已足以驗證 AOS 抽象，不需要先造分散式系統或安全容器。

## 不把 workspace 當 sandbox

**明確延後**：namespace、seccomp、cgroup、容器、權限降級與完整秘密管理。

`cwd` 或 Agent root 只是執行與資料位置，不是安全邊界。普通 Linux process 仍可能讀寫其他可存取路徑、連網、啟動 child 或留下外部副作用。文件與 UI 不得把「在自己的地盤工作」寫成作業系統隔離。

未來加入 sandbox 時，應作為 leaf executor 的執行政策；Function、Task tree 與 filesystem memory 的上層模型不必因此改名。

## 不承諾 exactly-once 或自動回滾

寄信、付款、HTTP 請求與任意檔案寫入無法靠一份本機紀錄變成恰好一次。AOS 能先保存 intent、保存可信結果、辨認 unknown，卻不能在 crash 後憑空知道遠端是否已完成。

**目前推薦**：結果不明就停下來，由上層使用業務查詢、去重 key 或人工修復。私有 workspace 可以丟棄未發布的檔案變更，但不能倒回已送出的網路或裝置效果。

## 多 writer 與私有工作區

**明確延後**：多個可寫 Task tree 同時直接修改同一 Agent root。

只讓它們彼此看見 active Tasks 仍不夠；兩者可能同時讀舊值、覆蓋 Git index，或在 Definition 改到一半時執行。近期基準仍是一棵可寫 Task tree。

後續若需要並行，順序建議是：

1. 先允許多個唯讀 snapshot 工作。
2. 再讓每個 Task 使用私有 workspace。
3. 正式 root 保持單一發布者，發布前比較 base version。
4. 發現衝突先拒絕，不做自動合併。

這仍不能回滾外部作用；workspace path 也不會變成新的 Agent ID。

## 跨機有兩個不同問題

**明確延後**：在線的 network AOS。

- **Git／檔案搬運**：把 paused-safe checkpoint 帶到另一台機器；新機預設 detached，不承接舊 PID、claim 或 active set。
- **兩台 AOS 在線合作**：還需要 owner 協調、網路協定、認證、檔案同步、衝突處理與斷線恢復。

第一項可先逐步實驗；第二項是更後面的分散式排程問題，不能只靠 `git push` 假裝完成。

## 非本機檔案系統

**明確延後**：NFS、SMB、FUSE、9P 與跨檔案系統原子發布。

目前落盤順序只能在明確測過的本機 filesystem 上聲稱有效。rename、lock、`fsync` 與目錄耐久性的語意在不同系統可能不同。未來 adapter 必須先跑相同 crash matrix，不能只因 API 名稱相同就宣稱等價。

自製 VFS 也不進核心第一版。普通 Linux filesystem 先充當 memory；需要遠端或特殊視圖時，再以明確 adapter 或 mount 加入。

## 路徑的非常規變化

**明確延後**：symlink 作 Agent ID、外力改名、path reuse 與搬移中仍繼續執行。

主線把 Agent root 的實際絕對 path 當 ID。Agent 自己要改名時，近期可先停止工作、使用 Git／檔案操作搬移，再重新 attach；風險由操作者承擔。未來 AOS 可增加受控 rename／relocate，但不為罕見案例先扭曲基本模型。

一般安全實作仍應拒絕資料存放區內意外出現的 symlink、FIFO 或不合規檔案；這和「Agent ID 是否支援 symlink」是兩件事。

## Tool、資料量與串流

**明確延後**：正式 Tool catalog、秘密注入、大型 attachment、streaming protocol、完整 GC 與配額。

未來仍以 leaf process 的 argv 與 raw streams 作最底層形式：

- 小型純量可放 argv。
- 結構資料可由上層約定 JSON stdin／stdout。
- 大型或二進位資料宜放 filesystem，以 path、大小與內容識別資訊傳遞。
- HTTP／server API 先由 curl-like 本機 adapter 表達，不新增 AOS 專屬網路 Function 種類。

這些是上層 binding 與資源政策；AOS Machine 不解析模型 Tool schema，也不把 stderr 當 Tool Result。

## 特定 coding agent 與團隊功能

**明確延後**：Pi coding agent、MCP、專屬 extension、自動發現整棵 child-agent hierarchy，以及完整 team leader 協定。

Agent 可自行在地盤中安排 `tools/`、`subs/` 或其他結構；AOS 不掃所有子資料夾猜哪個是 Agent。只有被明確建立與登記的 child root 才有自己的 ID 與寫入地盤。

適配特定 coding agent 時，應先包在 Agent 層或本機 executable adapter，不把其對話格式推入 AOS Machine。

## 之後加入功能時的守則

每個延後功能都要回答：

1. 它是新的 Function、executor、排程政策，還是 Agent 上層慣例？
2. 它會改哪一份正式狀態，誰是唯一寫入者？
3. AOS crash、網路斷線或資料衝突後，結果會是已知、unknown 還是明確拒絕？
4. 它是否需要改動核心 Function／Task 詞義？若需要，應先懷疑分層是否放錯。
5. 有沒有從最小正常案例到反例與中斷恢復的原型路線？

只要守住這些問題，AOS 可以由小到大擴充，而不必在第一版預先實作所有未來能力。
