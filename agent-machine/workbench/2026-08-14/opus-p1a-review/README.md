# P1a 外部架構評審包

這是一次**非正式、限範圍**的 AOS P1a 架構裁決。請先讀本頁，再依 [`SOURCE-PATHS.md`](SOURCE-PATHS.md) 的優先順序看需要的材料。

請將意見直接寫為同目錄的 `OPUS-REVIEW.md`；不要覆寫本包四個檔案，也不要另寫完整規格、程式或背景摘要。總長請控制約 **2,000–3,000 個繁中字符**，最多列 **5 個 blocking 問題**。若沒有 blocking，請直接說「可進 P1a-2」，並指出仍需保留的風險。

## 現在要裁決的模型

AOS 的核心是 **AOS Machine**：檔案系統是記憶體，Function 是可組合的指令。Function Definition 由明確的 callable path 承載：可先是 leaf file，資訊或責任變大時可長成 directory module，但入口必須明示。Function Call 是一份具體、不可變的呼叫請求；被 AOS 接受後，正在執行或等待結果的實例就是 **Task**。Task 也可建立 child Call，因此形成 parent/child Task tree；最終 leaf 會落到 Linux process call（`argv + stdin -> stdout + stderr + exit status`）。

在 **AOS Agent** 語境中，Step 與 Round 都是 Function：Step 是具原子操作角色的 Function；Round 是由一連串 Step 與工具呼叫組成的 composite Function。Agent root 是特殊 stateful directory：同一資料夾同時是 Definition、唯一 instance memory，以及預設 execution/effect place；因此該 root 預設 active capacity 為 1。這不把 capacity 1 倒灌到一般 Function Definition。

使用者偏好的直接入口候選為：

```sh
cd bot-a
aos exec ./ask "do something"
```

P1a 目前只驗單機、一個 writer、composite Task tree 與 crash recovery。它不是 Git image、中央排程、多機、LLM、完整 Agent UX 或正式 ABI 的規格。現行 Python 原型已驗 fake child outcomes；下一小步 P1a-2 只會接一個 deterministic P0 process case。

## 回覆格式

請依序回答 [`QUESTIONS.md`](QUESTIONS.md) 的三題：每題給結論（可接受／需改／不可接受）、最小理由、必要時的最小修正。不要重新敘述 AOS 背景，也不要擴大範圍。已知觀察見 [`KNOWN-FINDINGS.md`](KNOWN-FINDINGS.md)；除非你的結論不同，不必重找。
