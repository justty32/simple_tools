# Return、unknown 與 repair

Function Return 是 caller 關心的語意結果；Task state 是 AOS 對生命週期的判斷；process 的 stdout、stderr、termination 與持久證據又是不同角色。它們可以相互引用，不能因共用資料就互相改名。正式 Return ABI、Receipt 名稱、事件格式與 repair 操作都尚未決定。

## 方案 A：process 一結束就直接交回結果

把 exit、stdout、stderr 或 launch error 直接視為 Function Return。優點是互動快、最像 shell，對純 leaf runner 很直覺。缺點是 process 結束不保證 composite Task 已完成，也不保證 AOS 已把結果與 Call 的關係保存好；background child 或 crash 時尤其容易錯判。

**目前推薦：**只把它當 leaf 可觀察證據，不能單獨作需要重開與追溯的 AOS 結果規則。

**推翻條件：**若某個明確模式永遠不保存、不組合、無外部作用且不支援重連，可提供這種直接體驗；不得用它反推其他 Function 也安全。

## 方案 B：只有已知且可驗的結果才形成 Return

AOS 只有在結果與其依據完整、不可矛盾且已安全發布後，才對 caller 表示 known Return；非零 exit 或 signal 也可以是已知業務失敗，而非 unknown。優點是 caller 能分開「知道失敗」和「不知道是否已發生」。缺點是需要保存屏障與證據關係，互動時也必須能清楚顯示仍在等待確認。

**目前推薦：**以此作主線基線；具體欄位、檔名與驗證算法不在本頁固定。

**推翻條件：**若測試顯示某類完全無副作用的 Function 不需要持久結果，才可在該類另採方案 A；任何可能留外部效果的工作仍不能跳過此線。

## 方案 C：unknown 視同普通失敗並自動重試

好處是表面操作很省事，也可能提高暫時網路錯誤的成功率。壞處是 intent 已送出、結果證據尚未落盤時，效果可能其實已完成；自動重送可能重複寄信、扣款或改檔。它也把「結果不明」錯寫成已知失敗。

**目前推薦：**不採用作一般規則。unknown 是證據狀態，不是 retry 許可。

**推翻條件：**只有 Function 被明確限制為無外部作用，或外部服務提供可驗證的防重複／查詢能力，並由原型證明重試規則，才可對那一類工作限定使用；這仍不是 AOS 的 exactly-once 承諾。

## 方案 D：unknown 停住，再由 repair 取得新證據

先保存「已送出意圖、但沒有完整結果」的事實，維持 blocked／unknown；repair 可以檢查本機證據、向支援的服務查同一個操作，或由使用者明確另做一次。優點是避免自動重複效果，也留下可審查歷史。缺點是使用者要處理不確定性，repair 與外部系統的對應需要額外設計。

**目前推薦：**和方案 B 配合：若完整原始證據已在，只差可唯一推導的投影，recovery 可補齊；若仍有兩種可能，就停在 unknown，不捏造成功或失敗。

**推翻條件：**若未來定義出可機械驗證、跨服務可靠的 effect identity 與查詢規則，可把部分 repair 自動化；在那之前，repair 不應改寫歷史或假裝 rollback。

## 小原型該回答什麼

在 dispatch intent 已提交、外部 fake effect 已完成、結果標記尚未寫完時殺掉 manager；重開後必須是 unknown 且效果不重送。再測完整證據只差唯一索引時能否安全補回。這只檢驗窄 fault model，不決定正式 schema。背景見[持久保存候選](../../workbench/2026-08-14/idea-ledger/DURABILITY-CANDIDATES.md)與[主線持久狀態](../05-DURABLE-STATE.md)。
