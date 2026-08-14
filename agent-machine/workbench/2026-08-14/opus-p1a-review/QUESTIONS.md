# 三個裁決問題

請獨立挑戰下列暫定判斷；不要因為它是暫薦就順從。

## 1. Child Call 的保存位置

Child 的不可變完整 Call bytes 應放哪裡？

- **A：每個 child Task 自存 Call**：child 自足，會重複少量 bytes。**P1a 暫薦。**
- **B：只由 parent 保存，child 引用**：減少重複，但 parent 保存期耦合 child 正確性；目前原型採用，已知有 blocker。
- **C：內容定址儲存（CAS）**：以 hash 共享、每 Task 引用；較適合長期，但引入 GC／可攜性／publish 規則。

請裁決 P1a 應採哪個，並評估「P1a 先 A、長期再 C」是否是合理的分階段路徑。

## 2. Call、Task 與關係資料的切分

每個 root Task 也必須有自己的 Call。請檢查應如何切分：

- immutable Call：definition reference、argv/stdin、cwd/env、policy、版本／hash；
- Task：接受後的身分、狀態、receipt、durable events；
- parent/child relation：planned、linked、observed、slot、順序與引用。

問題是：每 Task 各自 ordered event stream，加上 parent 的 `planned -> linked -> observed` 關係，是否足以支持 restart/replay，而不需要掃目錄猜關係？請指出必要的 ownership、順序或 fail-closed 修正。特別檢查目前「parent 可能缺自己的 Call」與「Call 混入 task/tree fields」兩個問題。

## 3. Callable path 與 P1a-2 門檻

`aos exec ./ask "do something"` 中，明確 callable file 未來可成長為 directory module；「path 為 Function Definition、Call 固定已解析請求」是否是合理主線？請評估入口明示、definition generation/hash、recovery 不重新解析的最低要求。

綜合前兩題，請明確判斷：在採取你要求的最小修正後，是否**准許 P1a-1 進入 P1a-2**（只整合一個 deterministic P0 process case）？
