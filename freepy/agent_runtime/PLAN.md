# agent_runtime 規劃

**這份是實作規格，尚未寫程式。** 這層負責建立、限制、觀察與回收 agent instance；它不是 agentloop，也不負責團隊關係。

## 位置

```text
team_tools            組織、授權、資源帳本、任務
        │ request
        ▼
agent_runtime         真的 spawn / stop / collect
        │
        ▼
agentloop             一個 child 裡如何跑完一個回合
```

模型呼叫 `spawn_agent` 只是向可信任 supervisor 提出請求。受限 agent 永遠拿不到 Podman socket，也不能自己建立不受控容器。

## 模型工具

```python
spawn_agent(
    task,
    context="fresh",             # fresh | fork
    permissions=None,            # 不給 = supervisor 選安全預設
    resources=None,
    workspace="isolated",       # isolated | readonly
) -> agent_path

agent_status(agent_path) -> str
collect_agent(agent_path) -> str
stop_agent(agent_path, reason) -> str
```

`spawn_agent` 立即回 canonical path，不等待 child 做完。若父是 `/root/planner`、supervisor 核准的 child segment 是 `researcher`，結果就是 `/root/planner/researcher`。父子協作使用 `communication_tools`；`collect_agent` 只收最後狀態、結論、artifact/ref 與資源結算，不把 child 全歷史灌回父 context。

path 是人與模型看到的邏輯組織身分，不是宿主 filesystem path；三層共用根目錄下的 `agent_identity.py` 解析它。runtime 另配不可重用的 `instance_id`。同一路徑的 agent 結束後即使允許重建，也不能讓舊 event、reservation 或 mailbox ack 誤套到新 instance。v1 直接禁止 active path 重複與 reparent；移動子樹等於改身分，留到有 migration protocol 再做。

## 權限只能縮小

supervisor 以父 agent 的 effective policy 推導 child policy：

```text
child permissions ⊆ parent effective permissions
```

具體規則：

- tools、engines、network destinations 只能取子集合。
- mount 只能移除或由 `rw` 降成 `ro`，不能新增宿主路徑。
- secret 預設不繼承；只能要求父 policy 中標成 delegatable 的 broker route。
- capability 只能移除；不能新增 `NET_RAW`、container control 等能力。
- child 只能建立在自己的 path 直下；segment 與完整 path 由 supervisor 驗證，`.agent` 是保留 segment。
- identity、policy 與 sandbox args 由 supervisor 產生，不能由模型傳 raw argv。

只在同一 Python process 建一個新 `LLM` 物件，不足以實現這些保證。正式 `spawn` 必須是獨立 process/container；in-process child 只能標成 trusted simulation。

## 資源守恆

資源分兩種，不可只檢查 `child <= parent`：

### 可消耗預算

token、工具呼叫、模型步數等在 spawn 時先從父帳本 **reserve**。child 結束後只退還未使用部分；不能讓十個 child 各自拿到父剩餘 token 的完整副本。

### 同時容量

CPU、memory、PID、GPU visibility 等由父 cgroup／runtime pool 統一封頂，再分配 child 上限。即使帳本有 race，OS 上層 ceiling 仍不能被突破。

wall time 要區分 deadline 與消耗時間；等待工具使用者輸入是否暫停某項 timeout，由 tool policy 決定，不能偷偷改 Round/Step 計數。

## fresh 與 fork

`fresh` 只給 child：任務、角色說明、獲准工具與明確 memory links。它 context 最乾淨，適合正式委派。

`fork` 複製父 bot 在分岔點的 history，再加 child task。它適合針對性探查，價值是中間幾十步不進父 context。限制：

- 複製 history 不代表複製 Handle 或未完成的 tool call。
- 有 pending calls 時禁止 fork，除非先結清。
- workspace 預設 `readonly` 或 copy/overlay；不能把有副作用的 fork 叫 dry run。
- child 完成後由它產生結論；父只收到 summary + refs。

`fork_agent(...)` 可在之後成為固定 `context="fork"` 的便捷工具，不另造一套 runtime。

## 生命週期

```text
requested → reserved → starting → running
          → completed | failed | stopped | expired
          → collected → cleaned
```

每次 transition 寫 append-only event。start 失敗必須釋放 reservation；cleanup 失敗保留 evidence 並告警。`collect_agent` 可以重試且不得重複退還預算。

team layer 若要「產生下屬並交辦」，採 saga：reserve → runtime spawn → team register → task assign；任一步失敗便逆序補償，不能留下有 agent 沒帳、或有帳沒 agent 的幽靈狀態。

## 實作順序與測試

1. `policy.py`：permission subset、mount downgrade、delegatable secret。
2. `budget.py`：reserve/commit/refund，含並發鎖與 idempotency key。
3. 共用 `agent_identity.py`：canonical path、parent/ancestor、segment-safe subtree 判斷。
4. `records.py`：agent path + instance id、狀態機、event log。
5. `supervisor.py`：先用 fake process 驗 lifecycle，再接 Podman backend。
6. `tools.py`：只送 typed request，不接受 raw runtime args。
7. fork history copier 與 summary/ref 回收。

必測：不能升權、不能 spawn 到父 path 外、`/root/a` 不誤認 `/root/ab` 為子樹、active path 不重複、並發 spawn 不超賣、start 失敗退款、collect 兩次不重複退款、父停止可遞迴停止 child、pending tool call 不可 fork、child 沒有 runtime control socket。
