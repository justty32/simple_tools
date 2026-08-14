# P1a-2A：兩個假子工作串接

> **工作台 fixture，非正式 AOS ABI。** 本頁只安排下一個小實驗；既有 `sequence_two`、它的 golden bytes 與 process 規格一律不改。

## 要回答的一件事

Phase 1 已驗 root 的接受與恢復；既有 `sequence_two` 則固定為「first process、second fake」。兩者中間仍少一塊：從一個剛被安全接受的 root，能不能靠已保存的 root Call，持久地長出一棵有兩個子 Task 的樹並在中斷後接回來？

新 fixture 叫 `sequence_two_fake`。它的 first、second 都是已凍結的 fake recipe，不啟動程式。這是**補一項新證據**，不是重做 v2，也不是把 process 偽裝成 fake 成功。既有 `sequence_two` 仍保留 first 的 process Receipt 必須綁 attempt/P0 evidence 的規則。

## 本片故意不決定的事

- 不是正式 Function、Call、Task、Return 或 Receipt ABI。
- 不定義 Task 的正式起點；這裡只保證已 `planned` 的同一 ID 不會消失，`linked` 後才可執行。
- fake 沒有 `dispatch_intent`、`attempts/`、P0 UUID、subprocess、外部效果或重試。
- 不新增公開 CLI；測試可呼叫工作台內部入口。
- agent 忙碌、追加 prompt、條件 queue、同一 agent root 的並行寫入，完全另案。這片只驗一棵單 writer 的 Task tree，不能拿來允許兩個 Task 同時改同一資料夾。

## 固定資料來源與可達範圍

接受 root 時，`sequence_two_fake` 的 root Call 內保存 first、second fake recipe 及 first 的成功條件；它們隨 root `call.json` 一同凍結。child Call 只能由這份**已接受**的 root Call 產生：progress/recover 不讀原輸入、不叫 resolver，也不猜目錄中的檔案。

唯一可達路徑是：

```text
roots.jsonl 的 root_planned
  -> root Task
  -> root events.jsonl 的 child_planned / child_linked
```

不得掃描 `tasks/` 把孤兒目錄當 child。`child_planned` 使 child 可達，`child_linked` 後才 eligible。沿用 06/08 的 exact ref、payload、目錄大小上限、regular/non-symlink 檢查與 orphan 規則；沒有 relation 指向的東西仍不是 Task tree 的一部分。candidate、容量與 exact event grammar 見 [`10-P1A2-COMPOSITE-FAKE-FORMATS.md`](10-P1A2-COMPOSITE-FAKE-FORMATS.md)。

## 持久順序

空 store 的 root 先沿用 Phase 1 次序：

```text
root call publish -> root_planned -> root task.json
-> root accepted -> root_linked
```

root linked 後，對 first，再對 second，固定依序做：

```text
child call publish
-> parent child_planned
-> child task.json
-> child accepted
-> parent child_linked
-> child Receipt payload publish
-> child receipt_committed
-> parent child_observed
```

first 已被 observe 且符合 root Call 的成功條件，才可開始 second。兩個 child 都已 observe 後，才：

```text
root Receipt payload publish -> root receipt_committed
```

每個箭頭左側都必須先完成寫入與 fsync，才可做右側；任何 I/O 失敗即停止。fake 的 Receipt 必須只綁自己的 frozen fake recipe 與 Call ref，不能借用 process 或 P0 欄位。

若 first 的 fake Return 已知、但不符合成功條件：parent 仍先 `child_observed(first)`，再穩定呈現 `waiting_for_parent_policy`；不得規劃 second，也不得建立 root Receipt。

## 恢復規則

1. 取得既有 store 的獨占寫入權，先完整唯讀驗證 registry、所有已 reachable 的 Task、relation、payload 與 committed log prefixes。
2. 只可修正已驗證的最後一段 torn tail；修完重新完整驗證。
3. 從上述持久順序找唯一下一個缺口；一次只補一個 transition，寫完再驗證。
4. child 尚未 `planned` 時才從 frozen root recipe 產生其 Call；一旦 `child_planned` 已提交，Call ref 不得改。
5. 不叫 resolver、不開原 input；即使原 input 已刪除、變 FIFO，或 resolver 是 poison callback，也必須只靠已保存的 root Call 完成可恢復部分。
6. 遇到 committed bytes/ref/order/schema 不合、reachable 目錄異常或多個不可能缺口，回 corruption，不能用補 link 或掃 orphan 把它洗成合法。

達到 terminal state 後，連續 recover 五次，所有 authority bytes 必須不變。

## 最小測試矩陣

| 類別 | 必測結果 |
|---|---|
| 既有 root | 原有 10 個 accept/recover failpoint 全數串入；既有 47 項測試不可退化。 |
| composite crash | 新增 23 個 crash cuts，涵蓋五次目錄建立、兩個 child 的 publish/plan/materialize/link/Receipt/observe，以及 root Receipt；每次 writer 精確以 97 結束，recovery 後與 clean run authority bytes 相同。 |
| 來源封閉 | root 已 plan 後刪除原 Call input、把原 input 換 FIFO、或傳 poison resolver；recover 不讀它們且能依 frozen root Call 前進。 |
| 可達性 | registry→root→parent relation 才是唯一 tree；孤兒不 dispatch、不被認成 child、bytes 不被 recovery 改動。 |
| 反例 | child event 次序錯、ref/payload 不合、tamper、超過容量、linked 缺 task/accepted 都 fail closed。 |
| 結果 | first 成功才有 second/root Receipt；first oracle mismatch 只得到 `waiting_for_parent_policy`，second/root Receipt 均不存在。 |

23 個切點是 child 10 × 2 加 root 3；其中五次是目錄建立。精確 event/格式/容量規則見 [`10-P1A2-COMPOSITE-FAKE-FORMATS.md`](10-P1A2-COMPOSITE-FAKE-FORMATS.md)，hook 名仍只屬工作台。

## 成功時能聲稱什麼

只可聲稱：在單一 Linux filesystem、single writer、process-kill/failpoint 模型下，`sequence_two_fake` 從空 store 可經 root registry 建立兩層 fake Task tree，並可由 frozen root Call 恢復到相同持久 bytes。工作台的 eager fake runner 不是正式 scheduler；它不聲稱電源中斷安全、跨機、真正 process、agent busy admission/條件 queue，或同 root 多 Task 同時寫入安全。

相關背景見 [`03-PROTOTYPES.md`](03-PROTOTYPES.md)、[`06-FUNCTION-TASK-MODEL.md`](06-FUNCTION-TASK-MODEL.md) 與 [`08-P1A2-FORMATS.md`](08-P1A2-FORMATS.md)。
