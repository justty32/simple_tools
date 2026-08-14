# filesystem memory 的讀寫模式

「filesystem 像記憶體」不代表所有檔案都用同一種一致性規則。Definition、可變 memory、持久證據、本機執行狀態與外部世界各自有不同風險；`.aos` 布局、generation 算法與 Git 邊界仍未定。

## 先分資料，不先發明一個總開關

| 資料 | 要回答的問題 |
|---|---|
| Definition | 這次呼叫依哪版工作定義？ |
| 可變 memory | messages、設定與成果以哪個 base 發布？ |
| 持久證據 | 哪些接受、意圖與結果已不可改寫？ |
| 本機狀態 | 哪個 worker、claim 或 PID 在本機還有效？ |
| 外部世界 | HTTP、寄信或 root 外修改是否可能已發生？ |

本機狀態不能冒充可攜 image，snapshot 也不能把外部作用倒帶。以下四案是整體取向，可按資料類型組合，不能只挑好處。

## 方案 A：全部 live

Task 每次讀目前路徑，修改也直接寫公開 root。優點是最符合 shell 習慣，手動編輯與 agent 自我修改都直覺，實作成本最低。缺點是同一 Task 前後可能讀到不同內容，舊工作可覆蓋新資料；被殺時也可能留下半份檔案。

**目前推薦：**只保留為明確的低保證模式或早期實驗，不當預設持久模型。

**推翻條件：**若產品範圍真的只有單一 writer、短暫且無需重現的本地工作，可選它來換取簡單；一旦要保存、重開或有外部作用，就不足夠。

## 方案 B：checked-live

仍操作普通 root，但讀取時記下版本，在 dispatch 或發布前重比對；不一致就停下。優點是保留 live 的操作感，也能擋舊 Task 靜默覆蓋新 memory。缺點是檢查與真正 `exec` 或發布之間仍可能換檔，外部作用完成後才發現衝突也無法撤銷。

**目前推薦：**作小範圍 Definition 與 memory 發布保護的候選，不宣稱它提供固定執行映像。

**推翻條件：**若 fixture 證明這個空窗對目標 Function 已不可接受，或需要可重現的多檔 Definition，就要改用 snapshot 或縮小可承諾範圍。

## 方案 C：snapshot、branch 或 overlay

Task 讀固定副本並在私有工作區修改，最後比對 base 再發布。優點是較穩定、好重現，也能支援平行探索。缺點是衝突、清理、磁碟與發布交易都更複雜；丟掉 workspace 不會撤回已寄出的信或 API 請求。

**目前推薦：**明確延後完整 workspace 版本；先只拿它處理確實需要固定輸入的小實驗。

**推翻條件：**若兩個安全隔離的探索工作已是近期需求，且 checked-live 不能避免互相覆蓋，才值得做「衝突即停、不自動 merge」的最小版本。

## 方案 D：依資料類型混合

root 保持適合 shell 的 live 外觀；Definition 在作用前檢查；memory 先進 staging、再比 base 發布；Call、結果證據與 checkpoint 一旦提交就不原地改寫；外部作用另有 intent 與結果證據。本機 claim、PID 和 active set 留在本機，不混入可攜狀態。

優點是能把簡單操作與逐步安全提升並存。缺點是每區資料都要明說規則，不能只寫一個含糊的 `memory_mode`；跨多檔發布也仍需要實作證據。

**目前推薦：**第一版採這個思路，但每一小塊的檔案、hash、manifest 與 commit 順序仍是尚未決定。

**推翻條件：**若實作無法把資料類別、單一 writer 與發布點說清楚，就先退回更小的 checked-live 範圍；若未來需要強重現或平行發布，再有證據地擴到方案 C。

## 要先量的反例

在 hash 後、spawn 前替換入口，確認 checked-live 不能聲稱真正執行的是舊 bytes；讓兩個工作由同一 memory base 發布，確認後者明確衝突；讓一個 Task 產生新版 Definition，確認本次結果仍只綁原版。完整候選與測試清單見[filesystem memory 工作稿](../../workbench/2026-08-14/idea-ledger/FILESYSTEM-MEMORY-CANDIDATES.md)。
