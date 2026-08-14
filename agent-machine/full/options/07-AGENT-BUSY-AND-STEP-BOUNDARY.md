# Agent busy 與 Step 邊界選項

狀態：busy 三路是**使用者已定**；追加內容何時生效、paused 是否交出地盤，仍是**尚未決定**。任何方案都不得把三路重新合併。

## 不可改寫的三條入口

1. **普通新呼叫**：Agent root 忙碌就拒絕，清楚說明未收下；Task 數、queue 與工作目錄都不增加。
2. **明確追加**：內容屬目前 Task，不建立另一個 Task；先持久保存，等選定安全邊界才消費。
3. **明確排隊**：建立新 Task，並明寫目前 Task 到什麼狀況後才能競爭執行；條件成立後仍要另取 root 資格。

結果不明不能偷偷算成功或普通失敗。依賴它的排隊 Task 第一版應停下待處理，而不是猜測後續可跑。

Step 的「原子」目前只保證排程與持久發布邊界：同一 root 不在寫入 Step 中間插入另一棵 writer，parent 只看完整提交的 Return。它不保證 rollback、process 不會 fork，或外部作用 exactly-once。

## 方案一：下一個 Step 邊界採用追加內容

目前 Step 完成並提交 Return 後，Round 在規劃下一個 Step 前讀取追加內容。

- **優點**：最符合 Step 的原子角色；已 dispatch 的 argv、prompt 或 Tool Call 不會中途變義；crash 後容易判定內容是否已消費。
- **缺點**：長 Step 或卡住的外部服務會讓新訊息久等；結果 unknown 時沒有自然的下一步。

這是第一個原型的推薦。

## 方案二：Round 結束才採用

目前 Round 完整 Return 後，追加內容成為下一個 Round 的輸入。

- **優點**：單一 Round 的輸入最穩定；重播與說明最簡單。
- **缺點**：多 Step Round 的回應很慢；使用者補充可能到工作完成後才被看見。

適合把 Round 當較短 transaction 的 Agent，不宜作隱藏預設。

## 方案三：只在 paused／waiting-safe 採用

內容先保存，但只有 Task 到明確安全等待點時才交給 controller。

- **優點**：最不干擾正在進行的工作；可與人工把關結合。
- **缺點**：有些 Task 不會自然停；普通追加可能永久等候；`pause requested` 不能冒充 safe。

若採本案，UI 必須顯示「已保存但尚未採用」。

## 方案四：明確取消目前 Step，再建立後續 Step

不做中途注入；使用者另選擇取消／停止目前 Step，待它得到可信 Return 或 unknown 後，以新內容建立後續工作。

- **優點**：能回應「現在就改方向」；輸入邊界仍清楚。
- **缺點**：普通 Linux process 與外部 API 未必可安全取消；作用可能已發生；這是新操作，不等於普通追加。

此案只作長 Step 的後期逃生路徑。

## root 寫入權的共同約束

目前推薦同一 root 只有一棵可寫 Task tree。Round、Step 與 Tool children 沿用 parent 資格，不能重新排在自己後面。明確排隊 Task 即使條件已滿，也要等這棵 tree 釋放資格；同 root 的多 writer、Step 間交棒與私有 workspace 都是後續比較。

paused Task 是否保留 writer 尚未裁決。若它已 `paused-safe` 且明確交出地盤，resume 必須重新檢查 memory version 並競爭容量，不能保證拿回原位置。

## 目前推薦、驗證與推翻門檻

第一版採方案一：追加記錄先 durable，當前 Step Return 完整提交後恰好消費一次；unknown 時保留 pending 並讓 Task blocked。若沒有可識別的 Step 邊界，就拒絕聲稱支援追加，不用中途修改 live messages 冒充。

必測條件：

- busy 普通呼叫前後 Task 數與 queue 完全相同，回覆明說「未收下」。
- 追加後仍是同一 Task ID；在 Step 執行期間讀到的輸入不變，跨重開也只消費一次。
- 明確排隊若缺開始條件就拒絕；條件滿足但 root 忙時啟動數仍為零。
- parent／same-root child 同時可見而 writer slot 為一，且不形成 parent 等 child、child 等 root 的死鎖。
- 在「追加已落盤、Step 未結束」與「Return 已提交、尚未消費」兩點殺掉 Runtime，恢復結果唯一。

若實測長 Step 讓普通補充長期無法使用，先增加可觀察進度與明確取消方案；只有能維持 immutable input、unknown 保護與一次消費時，才考慮更細邊界。使用者定案與候選見[queue 候選](../../workbench/2026-08-14/idea-ledger/AGENT-QUEUE-CANDIDATES.md)。
