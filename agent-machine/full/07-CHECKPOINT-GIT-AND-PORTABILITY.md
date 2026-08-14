# Checkpoint、Git 與可攜性

checkpoint 是「可以理解並在之後明確續接的邏輯狀態」，不是正在執行的 process image。Git 能搬運已封存資料，不能搬走 PID、FD、socket、thread、lock，也不能證明另一台機器已停止。

## 只有穩態可以做 checkpoint

**使用者已定：**動態執行中的 Task 直接落盤很危險；pause 等穩定狀態才適合保存。**目前推薦：**至少分清三種情況：

- **pause requested**：只代表已要求停下；仍可能有 process、寫入或外部作用，不能做 checkpoint。
- **paused-safe**：已到可驗證的邏輯續接點；只有這種穩態可發布可攜 checkpoint。
- **diagnostic-only**：可封存部分證據供檢查，但不能在另一台機器自動續跑。

paused-safe 至少要同時滿足：

1. 沒有仍能修改正式 agent root 的受管理 process，process tree 已停穩，worker 不再持有寫入權。
2. 沒有 unresolved external effect，也沒有 unknown 被包裝成可續接結果。
3. 呼叫內容、已知結果、關係與所需證據都已完整持久化且互相一致。
4. 沒有未發布的 staging／workspace；Definition 與 Memory 的內容世代已固定。
5. 下一個邏輯續接位置明確，而且不依賴 heap、stack、fd、socket 或舊 PID。

若其中一項不成立，只能等待、blocked 或建立 diagnostic snapshot。一般 Linux process 無法因為寫了一份檔案，就在另一台機器從原指令中間繼續。

## 三種資料各有歸屬

**中央 AOS machine-local 權威**保存 queue、claim、active set、worker、執行中的 attempt、runtime 啟動代號及恢復判斷。它可在執行中持續寫 crash journal，但不能直接進 Git 冒充 checkpoint。

**agent root 內的 active 顯示**只是上述權威的可重建副本。它不參與 dispatch，離線時要標過期，也不屬可攜記憶。

**目前推薦：**可攜 `.aos` checkpoint 只收穩定語意；候選內容包括 messages、已提交的 Tool Results／Function 結果、必要的 Task 關係、下一個邏輯步驟，以及能驗證 Definition／Memory 內容世代的資訊。它必須排除 PID、FD、socket、lock、worker claim、active set、本機 queue、runtime 啟動代號、進行中 attempt 與未完成暫存檔。

這些是內容邊界，不是正式 schema。`.aos` 是使用者偏好；資料夾、單檔資料庫、manifest 或中央 store 引用如何組合，哪些內容進 `.gitignore`，都尚未裁決。

## 發布 checkpoint

**目前推薦：**先要求 pause，等到並驗證 paused-safe，再從同一份已提交世代建立 staging image。完整性檢查與持久發布成功後，才把中央狀態記成「checkpoint 已提交」，並讓可攜指標指向新版本。

發布程序必須讓 crash 後只看見完整舊版或完整新版，不能讓半份 image 成為目前版本。已提交 checkpoint 不再原地修改；下一次保存建立新版本。中央記錄、image 發布與可攜指標的精確 transaction 次序仍需原型，不應先發明固定檔名或資料庫 schema。

checkpoint 成功也不必自動 detach；是否釋放原機器的排程權要有明確操作與權威記錄。反過來，只有 diagnostic snapshot 時，不得用改名方式冒充 checkpoint。

## Git 搬的是封存歷史，不是 live ownership

在 AOS 產生的狀態中，只有已提交的可攜 checkpoint 適合 commit／push。agent root 原有的程式、文件、記憶與成果仍依 repo 自己的版本政策管理。Git commit 不是 pause 屏障，工作樹乾淨也不表示沒有 process 正在寫；中央狀態和 root 內本機顯示應排除在版本控制之外。

clone 或 pull 到另一台機器後，安全預設是 **detached 且 paused**：零自動 dispatch、零自動重送。attach 時才建立新的 machine-local 連接，重新檢查所需 Definition、Memory 世代、artifact、cwd、政策與 root 身分；通過後仍保持 paused，直到使用者明確 resume。

若來源機器沒有可驗證地 detach，單靠離線 Git 無法阻止兩台機器同時把同一 image 當成唯一 live owner。此時必須明確選擇另開分支身分，或等待來源端釋放；不能偷偷延續原 claim。真正的跨機單一擁有者、lease 與狀態交換屬後期 network AOS。

agent 的實際絕對 root path 是 Agent ID。clone 到不同絕對路徑後，不能假裝原機器的 path identity 與 process 仍相連；要視為新 root 身分、延續還是明確 fork，仍待可攜原型與使用者裁決。外力改名、symlink、path reuse 與跨機 path 搬移不進近期主線。

## 證據界線

**尚未決定：**checkpoint ID、保留與清理、Definition／Memory 世代算法、attach／detach 語意、Git 檔案集合與衝突處理。

**尚未驗證：**P1 工作台的 `<store>` 是本機 crash sandbox，不是 `.aos` 或 portable image。現有 fake 串接測試沒有涵蓋 checkpoint、Git、clone、attach、跨 filesystem 或 network ownership；這些只能列為下一階段要證偽的設計。
