# team_tools 規劃

**這份是實作規格，尚未寫程式。** `team_tools` 是有上下級、有組織的多 agent 工具集；它建立在 `communication_tools` 與 `agent_runtime` 之上。

## 分層

```text
team_tools
  組織樹、權限、資源帳本、任務與回報
       │                         │
       ▼                         ▼
communication_tools        agent_runtime
直接傳訊、通知             spawn/stop/collect、實際限制
```

mailbox 不是團隊。訊息只負責通知；成員、grant、allocation 與 task record 才是 authoritative state。讀掉一封信不能讓任務消失。

## 路徑就是組織位置

agent 使用 canonical path：

```text
/root
├─ /root/research
│  ├─ /root/research/web
│  └─ /root/research/papers
└─ /root/build
   └─ /root/build/tests
```

- `/root` 是組織 root agent。
- `dirname(path)` 是直接主管；下一段是直接下屬。
- 一個 leader 管理的 team 是自己的整棵 subtree。
- spawn child 只能在自己的 path 下建立一段，例如 `/root/build` 產生 `/root/build/tests`。

每段只允許安全名稱，`.agent` 保留給 synthetic filesystem metadata。ancestor 判斷必須比較 path segments，不能用裸 `startswith()`：`/root/a` 不是 `/root/ab` 的祖先。

這個設計借 Unix namespace 的好處：身分、隸屬與 scope 用同一個可讀地址表達。但它是邏輯 path，不是宿主路徑；三層共用 `agent_identity.py` 解析。**path 只表示位置，不自動授權**；一個叫 `/root/admin` 的 agent 若沒有 grant，仍沒有管理能力。

v1 不允許 reparent/rename active subtree。路徑移動會同時影響身分、mailbox、task assignee 與 audit，必須等有 migration protocol 才能安全做。

## 資料布局

```text
<org-root>/
  organization.json
  members/root/.../<agent>.json
  grants/root/.../<agent>.json
  allocations/root/.../<agent>.json
  tasks/<task-id>.json
  reports/<task-id>/<report-id>.json
  events/<sortable-id>.json
```

member 記 canonical path、instance id、role label、status、created/seen。主管由 path 推導，不另存一份可能漂移的 parent。

event log append-only；其他 JSON 是目前 snapshot。所有 mutation 都帶 operation id，重試不得重複扣資源、重複 spawn 或重複完成任務。

## 權限模型

effective permission 是 supervisor 已驗證、正在生效的能力，不是 agent 自己宣稱的 request。類型至少包括：

- 可用 tools、engines。
- workspace mounts：path 與 `ro`/`rw`。
- network destinations/protocol。
- 可委派的 broker/secret routes。
- `spawn`、`grant`、`allocate`、`assign`、`stop_descendant` 等管理能力。

授權遵守單調縮減：

```text
child grant ⊆ grantor effective permission
```

`grant_permissions` 只能授予自己的 descendant，預設只准 direct child；跨層需明確 `manage_subtree`。撤權要寫事件並通知 runtime 收斂 effective policy；若某權限不能在不中斷 process 的情況下撤除，就停止並用新 policy 重啟，不能只改 JSON 假裝已生效。

## 資源模型

資源由 `agent_runtime` 的 budget pool 實際保留，team_tools 只提供組織語意與帳本視圖：

- consumable：tokens、steps、tool calls、金額。
- capacity：CPU、RAM、PID、GPU visibility、concurrency slots。
- artifacts：workspace、memory refs、dataset 的 `ro`/`rw` 使用權。

`allocate_resources` 必須先 reserve 才寫成功；失敗不留下 allocation。父 agent 的十個 child 不能各自複製父剩餘額度。回收未使用資源要有單一 settlement event，重試不得重複退款。

## 任務模型

```text
draft → assigned → accepted → running
      → completed → reviewed → closed
      → failed | cancelled
```

task 至少包含 issuer、assignee path + instance id、parent task、brief、deliverables、permission grant、resource reservation、deadline 與狀態。

報告是獨立、不可覆寫的 record：progress、blocked、completed、failed。正文保持短，較大的輸出用 `memory:` Markdown links 或 artifact paths。通知透過 `communication_tools` 寄出，但 task/report JSON 才是事實來源。

轉派任務會建立 child task，資源從原 task 的 reservation 再切分；不能新生預算。上級可查看 subtree task，但同儕不能因路徑相近就讀取彼此 private task。

## 模型工具

| 工具 | 作用 |
|---|---|
| `team_status` | 看自己的 path、主管、下屬、grants、allocations、tasks |
| `grant_permissions` | 對下屬授予自己的權限子集 |
| `allocate_resources` | 對下屬／task 保留資源 |
| `assign_task` | 建立 task、授權／配額並通知 assignee |
| `report_task` | 向 issuer 寫 progress/blocked/final report |
| `review_task` | 接受、退回或關閉下屬的 completed task |
| `spawn_subordinate` | 組合 runtime spawn、member register 與初始 task |

`spawn_subordinate` 是高階 saga，不重寫 spawn 技術：

```text
reserve resources
→ derive child policy
→ agent_runtime.spawn
→ register member
→ assign initial task
→ send notification
```

失敗時逆序補償。模型不直接呼叫 `join_team`；member identity 只有 supervisor 成功建立 runtime 後才能註冊。

## 回合的關係

一個 task 可跨多個 Round；一次 Round 也可處理多筆相關 task。兩者不能用同一個 id 或計數。

主管在下屬 Round 進行中追加指令，應由 supervisor 經 communication/control plane 送到該 Handle 的 instruction queue；普通 mailbox 到信不會自行打斷模型。工具內部向使用者要輸入則仍屬工具呼叫，不是 task report 或新 Step。

## 不變條件

- 所有管理 mutation 都檢查 actor canonical path、instance id 與 effective grant。
- path ancestry 不等於權限；權限也不能作用到 grant scope 外。
- 權限只能縮小，資源必須守恆。
- task notification 可重送，task transition 不可重複執行。
- 歷史 member、task、report、event 不刪除；active snapshot 可更新。
- communication、runtime 或 ledger 任一步失敗都不能回報完整成功。

## 實作順序與關卡

1. 共用根目錄 `agent_identity.py` 的 canonical path parser。
2. `store.py`：member/grant/allocation/task snapshot + append-only event。
3. `authority.py`：ancestor scope、permission subset、artifact access。
4. `tasks.py`：狀態機、report、idempotency。
5. `tools.py`：先 status/report，再 grant/allocate/assign。
6. 接 communication notification。
7. 最後接 `spawn_subordinate` saga 與真 runtime。

必測：path prefix 陷阱、同儕越權、跨 subtree 越權、grant 升權、資源超賣、重試冪等、非法 task transition、通知失敗仍可查 task、spawn 中途失敗完整補償、舊 instance 不能操作同路徑的新 member。
