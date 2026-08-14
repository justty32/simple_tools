# Step、Tool 與 Round

Step、Tool、Round 不是 AOS Machine 的三種新指令；它們是 Agent 語境中 Function 扮演的不同角色。這個分層讓 Agent 可以成長，而不必把 LLM 詞彙塞進底層排程器。

## 共同的 Function／Task 規則

### 使用者已定

- Step 是有原子操作角色的 Function。
- Tool 是 Agent 可使用的 Function；最底層可沿用 Linux process 形式。
- Round 是由一串 Step 與 Tool 呼叫組成的 Function。
- 正在執行中的每個 Function 實例都是 Task。

因此一個 Round Task 可以擁有 Step Tasks、Tool Tasks；某個 Tool 若內部又呼叫其他 Functions，也可再有 children。這是一棵 Task tree，不是把所有執行都壓成一個 PID 或一列扁平 messages。

## Step 的「原子」是觀察邊界

**目前推薦**：先把原子分成三層說清楚。

1. **排程邊界**：同一 Agent root 不在 Step 中間插入另一棵會修改相同地盤的工作。
2. **持久發布邊界**：Step 的新狀態與 Return 只在完整提交後對 parent 可見。
3. **外部副作用**：普通 process、網路與任意檔案寫入通常不能回滾，也不保證 exactly-once。

第一版力求前兩項，不承諾第三項。Step 可以在內部呼叫 children，還是必須永遠是 leaf，**尚未決定**；「原子」不應被偷寫成「一定一個 Linux process」。

pause 通常在 Step 邊界生效。正在執行的 leaf 若結果不明，Round 停在該 child，不自動重送，也不捏造 Step Return。

## Round 是組合式 Function

Round controller 讀取已完成 child 的可信 Return，保存自己的進度，再決定下一個 Function Call。常見軌跡可能是：

```text
Step Task → Tool Task → Step Task → … → Round Return
```

這只是常見形狀，不是硬編碼模板。Round 可以不使用 Tool，也可以等待使用者、呼叫 child Agent，或在明確條件下停止。

AOS Machine 不解析模型回覆來推導下一步；Agent controller 才懂 prompt、messages、tool requests 與產品失敗。Composite Task 等 child 時不必長期占用 CPU executor，但仍可能持有 Agent root 的邏輯寫入資格。

## Tool 定義與一次 Tool 呼叫

Tool 是 Agent 對模型或 controller 暴露的語意角色，不等於 executable 檔案、Function Call 或 Task。

可分開理解：

- **Tool 說明**：名稱、用途與模型可見的輸入形狀。
- **Tool 接法**：把已驗證輸入轉成一個或多個 Function Calls。
- **Tool Call**：模型或 controller 提出的單次請求。
- **Tool Task**：該 Function Call 被 AOS 接受後的執行實例。
- **Tool Result**：Agent 根據可信 Returns 整理後，配回原 call id 的上層結果。

Machine 不解析 Tool schema，也不把 process Receipt 直接當 Tool Result。無效 JSON、未知名稱或 policy 拒絕可能在建立 Task 前就被拒絕；Agent 仍須產生可配對的上層錯誤，但不可假造「曾執行」的證據。

## Tool 到 leaf process 的三種常見接法

**目前推薦**：底層始終保留普通 Linux process ABI，上層依資料選擇表面形式。

### 小型參數

用 argv array 傳遞，不經 shell 再解析。適合字串、旗標與既有 CLI；秘密不宜放 argv。

### 結構資料

新工具可提供 machine mode，以有界 JSON 經 stdin／stdout 傳遞；stderr 專供診斷。這是 Tool binding 的約定，不是所有 Function 都必須使用 JSON。

### 大型或二進位資料

把 bytes 留在 filesystem memory，只傳 path、大小、媒體種類與內容識別資訊。Path 是位置，不等於不可變內容；要重播時仍須選擇 live、checked 或 snapshot memory 模式。

HTTP／server API 先包成 curl-like executable adapter：一次 request 仍是 leaf Function，不新增 AOS 專屬 HTTP 指令。

## 一個 Tool Call 可以展開多個 Functions

簡單 Tool Call 可對應一個 leaf Function；重工作可能展開成多個 Steps 或 child Task。若 children 需要各自的 pause、容量、Receipt 與 crash recovery，就應讓 AOS 看見它們，而不是由一個巨大 opaque process 假裝全部完成。

若 facade process 自己偷偷 spawn helpers，AOS 誠實地只看見一個 leaf Task；它不能對內部 children 提供逐步排程保證。兩種方式都允許，但文件不得混稱。

多個 child Returns 最後由 Tool Function 組成一份 Tool Return，再由 Agent 投影成一個配對 Tool Result。不存在「虛構一個 aggregate process exit code」的做法。

## Agent Call、Round 與長期記憶

人的一次 prompt 可建立一個 Round Call，這是**目前推薦的第一個表面模型**，不是 Machine 硬規則。較長 goal 是否再包一層 Agent Function，仍可後續比較。

對話 messages、Tool Result、workflow cursor 與長期記憶都保存在 Agent 自有檔案中。它們可以引用 Task／Return 證據，但不是 AOS Runtime 的唯一真相。FreePy 的舊 `Step = ask()` 與「整批 tools 不算 Step」只作歷史，不再約束 AOS。

## 錯誤分層

至少要分三層：

- **leaf process Return**：exited、signaled 或 launch error，以及兩條 raw streams。
- **Tool 語意結果**：輸入無效、權限拒絕、HTTP／應用錯誤或成功產物。
- **Round／Agent 結果**：是否還要追加 child、等待、失敗或回覆使用者。

非零 exit 不自動等於 Round 失敗；HTTP 404 也不應塞成 Linux exit status。相反地，process exit 0 只證明 facade 完整結束，不一定代表業務成功。

## 尚待決定

- Step 是否允許可見 children，以及原子發布包含哪些 Agent memory。
- Agent Call、Round Call 與長期 goal 的一對一關係。
- Tool catalog、版本凍結、秘密、streaming 與大型產物的正式格式。
- Tool policy 應由 C++、Janet 或 Agent controller 哪一層負責。

這些選擇都可在 Agent 層演進，不應反向改掉 Function／Task 的 Machine 核心。
