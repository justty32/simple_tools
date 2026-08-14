# 2026-08-14 收尾導覽

這是明天接手用的工作台交接，不是正式規格。今天重點是留下實測證據，並分開「已決定」與「暫時採用」。

## 3 分鐘摘要

- AOS 的方向是：Function 像 CPU 指令、檔案系統像記憶體。Linux process 是最底層的一種 Function，不是唯一形式。
- 使用者稱正在執行的 Function 實例為 Task。本輪原型為了保存，暫用固定 ID 貫穿排隊、執行與完成；這只是暫選，Task 不是 PID。
- agent 的絕對資料夾是它的 ID 與地盤。同一地盤預設不要同時有兩份可寫工作。
- agent 忙碌時，普通新呼叫直接拒絕且不建立 Task；追加內容與建立排隊工作是使用者明確選擇的另外兩條路。
- P0 已驗單一 Linux process 邊界；P1 已驗部分 Task 保存與重啟讀回。兩者都不是完整 AOS，也未證明斷電、多台機器或多人同寫。

明天的總入口是[下一輪決策包](round-2-decision/README.md)。

## 用 C++ 工程師熟悉的類比理解 AOS

把 AOS 想成 Linux 與上層工作之間的小型 job runtime：不只啟動程式，也保存「準備做什麼、是否已開始、結果是否可信」。

- **AOS Machine**：管理程式與其所在機器；重開時先看留下的記錄。
- **Function**：可重複呼叫的工作定義，像函式或 build target；可從執行檔長成資料夾模組、Step 或 Round。
- **Task**：使用者稱「正在執行的實例」為 Task。本輪為保存而暫用固定 ID 貫穿排隊、執行與完成；某次 process 啟動只是內部一次嘗試，這不是正式定義。
- **Agent root**：agent 自己的 repo 根目錄；同時寫會有覆蓋與 Git 互踩風險。

這不是既定 C++ API 或命令列。使用者偏好 shell 內操作，但最後介面仍待比較。

## 今天真正已定 vs 原型暫選

### 使用者已定

- AOS 的名稱與核心比喻；AOS 要處理自己的保存與重開。
- 資料夾可承載 Function；Function 可呼叫 Function；Step、Round 都是 Function。
- 使用者稱正在執行的 Function 實例為 Task；未指定它是否涵蓋排隊或完成後紀錄。
- agent root 絕對路徑是 ID 與地盤；預設避免同 root 多個可寫實例。
- 偏好 `.aos`，agent root 應能進 Git；保存位置與內容仍未定。

來源見[使用者方向](idea-ledger/USER-DECISIONS.md)。

### 為實驗暫選

本輪為保存而暫用「接受時配置固定 Task ID；每次 process 執行只是內部嘗試」。沒有可信完整結果就標成結果不明，不偷偷重跑。這不是正式承諾；保存期限與結果形式仍待選，見[本輪決定表](round-2-decision/01-DECISIONS.md)與[未決問題](round-2-decision/02-OPEN-OPTIONS.md)。

## agent 忙碌的三條路與同 root 風險

1. **普通新呼叫**：root 忙就拒絕；零 Task、零工作目錄、零偷偷排隊。
2. **明確追加**：不建第二個 Task，而是交給目前工作在下一個合適邊界取用；邊界細節還需原型。
3. **明確建立排隊 Task**：建立固定 ID，並寫明目前工作到什麼狀況後才能開始。

排隊工作不能只看前一個 Task；還要確認 root 是否可安全寫入，兩項都成立後才可啟動。父工作叫同 root 子工作時，子工作不能重新等待 root，否則會自己卡死；目前候選是同一棵呼叫樹共用可寫資格。

並行有三案：**完全序列化**（第一版基準，一棵可寫工作樹獨佔 root）、**受限並行**（可同時計算，但一次只發布一個）、**私有工作區**（各改副本再依序發布，外部影響無法倒回）。後兩案不急著實作。[取捨紀錄](idea-ledger/OPUS-03-TRIAGE.md)

## P0、P1 證了什麼，沒證什麼

**P0** 聚焦單一 Linux process。Python 與 C++ 都驗到：不經 shell 拼字串時，參數、輸入、兩條輸出與結束原因可分開取得；兩條輸出管須同時處理。Python 另驗到：已記下要執行卻沒有可信結果時，重開不自動再跑。[Python](p0-function-python/README.md)／[C++](p0-function-cpp/README.md)

P0 不證明斷電後資料完整、資源限制、環境可重現、跨檔案系統，或 Task/Agent 的完整行為。

**P1a-1 v2** 用假的 leaf 執行器驗到：Task 自存自己的 Call；父子關係可另外保存；已知結果重開後只補齊紀錄，結果不明不重做。[說明](p1a-task-tree-python-v2/README.md)

**P1a-2 第一階段**獨立重跑 47 項測試各 4 次，共 188 次。它只驗最上層工作被接受並保存後，重開入口讀回的結果相同；尚未執行 child 或真 process。[三分鐘示範](p1a2-phase1-demo/README.md)

**P1a-2A 的兩個假 child 串接已通過最終 gate**：WSL 全套 58/58 綠。它驗到 `sequence_two_fake` 的內容與引用逐 byte 一致、root → first → second 的 13 份關鍵記錄、23 個組合式中斷點加 10 個 root 中斷點後可穩定重開；也驗了容量、同名暫存碰撞、來源封閉與竄改反例。這只支持 single writer 的假 child 保存順序；仍無真 process、Agent 或 scheduler。[原型路線](round-2-decision/03-PROTOTYPES.md)

Janet 小實驗支持它做無副作用的資料檢查與建議；啟動 process、重新檢查資料、寫下會影響正式狀態的內容仍留給 C++／Runtime。它尚未在 Linux 跑過。[說明](p0-function-janet/README.md)

## Opus：採用與駁回

Opus 認可固定 Task ID、結果不明不自動重做、C++ 守住會改正式狀態的邊界，以及父子工作共用可寫資格以避免自我卡死。它原本建議普通呼叫預設排隊，但使用者後來明確覆蓋：今天採普通呼叫拒絕、明確追加、明確排隊三條路；Opus 的補充也接受此修正。

它建議暫不投入雙 ID 比較、四種 C++/Janet 接法等量實驗、SQLite、跨機器與路徑搬移，先驗最小保存與並行反例。[原始審查](opus-monitor/OPUS-REVIEW-03.md)／[補充](opus-monitor/OPUS-ADDENDUM-03.md)

## 為何今天不急寫正式規格

Task 保存、Step 邊界、資料夾 Function 入口、`.aos` 可攜內容與跨機接手都還會改變設計。今天先以 Python 找反例、C++ 固化 Linux process 邊界、Janet 只做純資料建議；有證據且不違反使用者方向的部分才進正式文件。

## 明天可執行的順序

1. 凍結 P1a-2A 的 58/58 證據邊界；只寫它實際驗過的假 child 保存順序。
2. 依[原型路線](round-2-decision/03-PROTOTYPES.md)只做下一小片：把一個假的 child 換成 P0 真 process 證據；不要同時加 Agent、Git、跨機或完整排程。
3. 做忙碌的四步小原型：分開拒絕與排隊、驗父子不自我卡死、驗 root 內可見狀態安全更新、驗兩次檢查與重開。
4. 每片完成後更新「已定／暫選／待選」的證據邊界；不把程式暫定名稱直接搬進正式規格。
5. 使用者有空才看[待回覆小卡](../../wait_user/README.md)；不回覆不阻塞工作。

## 約 30 分鐘閱讀路線

**5 分鐘**：讀本頁摘要與「今天真正已定」，先分清產品方向和原型選擇。

**10 分鐘**：讀[決策包](round-2-decision/README.md)、[決定表](round-2-decision/01-DECISIONS.md)與[未決問題](round-2-decision/02-OPEN-OPTIONS.md)，理解下一輪只驗哪些窄問題。

**10 分鐘**：讀[P0 Python](p0-function-python/README.md)、[P0 C++](p0-function-cpp/README.md)、[P1a-1 v2](p1a-task-tree-python-v2/README.md)；先看各自的證據邊界，不必逐行看程式。

**5 分鐘**：讀[Opus 取捨](idea-ledger/OPUS-03-TRIAGE.md)與[忙碌補充](opus-monitor/OPUS-ADDENDUM-03.md)。讀完應能回答：AOS 解什麼問題、哪些結論有測試、哪些仍是候選、明天為何先做小原型。
