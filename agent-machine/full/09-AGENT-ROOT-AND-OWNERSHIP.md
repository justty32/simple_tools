# Agent root 與地盤

Agent 是 AOS Machine 之上的重要擴充。Machine 只需要懂 Function、Task、檔案與執行；Agent 層才加入 prompt、記憶、Step、Tool、Round 與 team。

## 一個資料夾就是一個 Agent 地盤

### 使用者已定

Agent root 的實際絕對資料夾 path 就是 Agent ID，例如：

```text
/home/mine/agents/bot-a
```

這個資料夾是 `bot-a` 的地盤：承載它的工作定義、持久記憶與自我修改所影響的位置。同一層不再放第二個 Agent 與它共同做主。

用 C++ 類比，它接近有狀態的 function object：目錄內檔案像成員資料，呼叫入口像 `operator()(prompt)`；每次執行仍由 AOS 建立 Task，而不是複製出另一個同名 Agent。

Agent root 是 Agent 層的特殊 Function Definition。一般 Function 的定義位置、工作目錄與狀態位置可以分開；不能因 Agent 的特例，就規定所有 Function 都必須在自己的定義目錄內修改自己。

## 身分與可呼叫性

普通資料夾不會因名稱或結構看起來相似就自動成為 Agent。**目前推薦**：只有經明確建立或 attach、且能驗證其宣告者，才是可呼叫的 Agent root。

AOS 不向上搜尋最近的 `.aos` 猜身分，也不向下掃所有子資料夾猜 child Agents。從 root 開啟的互動環境即使 `cd` 到 descendant，Agent ID 仍是原 root，除非使用者明確切換到另一個已登記 root。

Symlink、外力改名、path reuse 與跨機 path 搬移屬非常規狀況，近期不扭曲主線設計；限制見 [13](13-LIMITS-AND-LATER.md)。

## 同一地盤的寫入權

### 使用者已定

同一 Agent root 同時有多個 Task 活躍時，資料夾內必須看得見，Agent 也要知情；這些資訊由 AOS 管理。

### 目前推薦

第一版同一 root 只允許一棵可寫 Task tree。Round parent、Step 與 Tool children 可同時出現在 active 清單中，但共用一份 root 寫入資格；它們不互相排到對方後面。

中央 AOS 才是 active set 與寫入資格的真相。root 內顯示只是方便 Agent 閱讀的本機副本：可以被重建、可標示過期，不能拿來搶容量或證明目前沒有 writer。

普通新 Agent 呼叫在 root 忙碌時直接拒絕。使用者仍可明確追加 prompt 到目前 Task，或建立有開始條件的新 Task；完整排程見 [08](08-SCHEDULING-AND-EXECUTION.md)。

## Agent Function 的輸入與影響

Agent Function 的上層輸入通常是 prompt，但 AOS Machine 不需要理解 prompt。Agent controller 把它保存為自己的語意資料，再建立 Step／Tool Function Calls。

Agent 的 Return 可以包含回覆、產物引用與新的記憶狀態；精確共同格式尚未決定。Machine 只負責 child Tasks、leaf process 證據與耐久邊界，不把 stdout 直接冒充 Agent 回覆。

Agent 會修改自己，因此呼叫的影響不只是一串 messages：也可能改工作檔、設定、工具或長期記憶。哪些改動算完成、何時發布為新 memory version，仍需原型；第一版先以單 writer 避免交錯。

## root 內可攜與本機資料

### 使用者已定

Agent root 應能直接放進 Git；`.aos` 是偏好的 AOS 相關位置。一部分運行中 Task 在安全穩態時也希望能保存、commit 並在另一台機器閱讀或續接。

### 目前推薦

root 內至少概念上分三類：

- **Agent 自有內容**：Definition、記憶、workflow、tool／module 與一般工作檔。
- **可攜 AOS 語意狀態**：已封存的 Task tree、Call／Return 關係與 checkpoint。
- **本機執行資料**：active claim、PID、socket、lock、workspace path 與 AOS 啟動代號，應被 Git ignore。

精確 `.aos` layout 尚未決定。中央 AOS 不應在 root 內留下另一份互相競爭的完整真相；本機執行資料也不可混入 checkpoint。詳見 [05](05-DURABLE-STATE.md) 與 [07](07-CHECKPOINT-GIT-AND-PORTABILITY.md)。

## child Agent 與 team leader

### 使用者已定

Agent 可以是 team leader；它可以自行管理子資料夾中哪些地方是 child Agent 地盤。`tools/`、`subs/` 等只是推薦布局，不是 AOS 保留字。

child Agent 必須被明確建立為自己的 root，才有獨立絕對 path ID 與寫入地盤。普通 module、tool directory 或任意 descendant 不會自動升格成 Agent。

parent Agent 可呼叫 child Agent Function；該呼叫形成另一個 Agent root 的 Task tree，因此兩個 roots 可各自使用自己的寫入資格。完整 team orchestration、child discovery 與權限協議明確延後。

## 從單檔長成 module

**目前推薦**：沿用 dev-guide 的漸進路徑，而不是一開始預造大量空目錄。

1. 小工作先是一個可呼叫檔案。
2. 承載太多程式與說明時，升為 directory module。
3. 需要可觀察 child Calls、排程或恢復時，成為 composite Function。
4. 需要自己的持久人格、地盤與獨立寫入權時，才明確建立 child Agent root。

「變成資料夾」只表示模組化，不會自動產生 Agent 身分；「拆成 child Functions」也不等於拆成 child Agents。

## 所有權不是安全隔離

地盤規則是 AOS 與 Agent 的協作契約，不是 sandbox。沒有額外 Linux 限制時，Function 技術上仍可能寫到 root 外或竄改保留資料。AOS 要能驗證自己的 authority，但第一版不宣稱能阻止惡意程式。
