# Task 的生命週期與身分

使用者已定的是「正在執行中的 Function 實例叫 Task」，而不是 Task 從接受、排隊、pause 到保存完成後的完整字典定義。本頁比較 AOS 如何讓工作跨過 queue、重開與 process 消失；任何 ID、狀態名、目錄或命令都尚未固定。

## 方案 A：Task 隨 PID 生滅

只有 process 活著時才有 Task；結束就消失。優點是最貼近 Linux、一次性 runner 最好做。缺點是 PID 會重用，queued、paused、等待 child 與完成結果都沒有穩定身分；AOS 重開後也難說同一工作是什麼。

**目前推薦：**只把它當 leaf process 的內部觀察，不作 AOS 對人或 parent 的公開模型。

**推翻條件：**若產品明確限於不保存、不排隊、沒有 child 且無外部作用的一次性 runner，可把這案當該窄模式；它仍不能替代完整 AOS 的 Task 模型。

## 方案 B：先有 Call，dispatch 才有 Task

接受請求時只產生 Call 身分，真正 dispatch 才建立 Task。優點是字面上最嚴格遵守「執行中才叫 Task」，也把請求與實例分得乾淨。缺點是使用者、parent 與 recovery 都要跟著兩個 ID；剛升格時 crash 還要維護額外關係。

**目前推薦：**保留為重要對照原型，不先視為較純粹就較好。

**推翻條件：**若雙 ID 在 parent-child、pause、重連與 crash fixture 中反而明顯減少歧義和狀態轉換，且使用感可接受，才值得提升為產品候選。

## 方案 C：安全接受後固定 Task ID

Call 通過驗證並安全落盤後，配置一個固定 Task 身分；它可經過排隊、執行、等待、暫停與完成記錄。優點是命令列端、parent 與 recovery 都只追一個身分，尚未 dispatch 的工作也能在重開後辨認。缺點是把 Task 一詞擴到「尚未執行」和「已完成的紀錄」，需要清楚說明這是 AOS 的延伸。

**目前推薦：**作第一版公開模型的候選，但只標為目前推薦，並非使用者已定。

**推翻條件：**使用者若要求 Task 嚴格只指正在跑的實例，或方案 B 的小原型證明雙 ID 更清楚且不增加 crash 風險，就回到方案 B。

## 方案 D：固定 Task 加內部 attempt

在方案 C 之下，每次實際啟動都留下 attempt；PID、worker、啟動世代與 process 證據屬 attempt，Task 身分不變。優點是能分清「同一工作」與「第幾次啟動」，也不把 process crash 假裝成新 Task。缺點是格式與診斷更多；若把 attempt 直接暴露給日常使用者，容易被誤解成自動重做許可。

**目前推薦：**若採方案 C，Runtime 可用本案保存診斷，而一般畫面只先呈現 Task。attempt 不是正式欄位，也不是 retry 承諾。

**推翻條件：**若第一個安全模型根本不允許 resume 或第二次啟動，attempt 只會增加噪音，可先不實作；一旦要處理重開或 pause，再重新比較。

## 共同安全線

- 拒絕的 Call 不得遺留看似已接受的 Task；Agent busy 的普通拒絕更是使用者已定的零 Task。
- process 結束最多表示某次 attempt 結束，未必表示 composite Task 完成。
- 缺完整結果時維持 unknown，不因有 attempt 就自動再做一次；明確重做也可能應建立新的 Task，尚待決定。

下一輪應用同一組 crash 情境比較 B、C、D：安全接受後 dispatch 前重開、parent 等 child、pause 後重連，以及 unknown 時不自動產生第二次啟動。背景見[Task 生命週期候選](../../workbench/2026-08-14/idea-ledger/TASK-LIFECYCLE-CANDIDATES.md)。
