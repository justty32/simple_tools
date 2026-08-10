# Dormant bot 的喚醒、待命與沉睡

日期：2026-08-10；本篇是 [`ADAPTERS.md`](ADAPTERS.md)／[`CONTROL-PLANE.md`](CONTROL-PLANE.md) 的 lifecycle 分卷。

## 結論

`dormant` 與 `standby` 都不應常駐 Python object、thread、process 或 endpoint connection。兩者差在**是否武裝自動喚醒規則**：

| 狀態 | 意義 | 保留資源 |
|---|---|---|
| dormant | 只存 bot record；普通來信／任務不自動喚醒 | storage；既有 task budget |
| standby | 無執行體，但 supervisor 已索引允許的 wake triggers | storage＋少量索引 |
| queued | durable WakeIntent／Turn 已入隊，尚未取得 endpoint | reservation，不佔 slot |
| active | Turn 正在 Round 或 tools；有 activation lease | 實際 endpoint/tool capacity |
| draining | 已要求沉睡；不再開新工作，正在到安全邊界 | 尚未釋放的當次 lease |
| faulted | record、policy、history pairing 等不能安全 hydrate | 不自動 retry |
| retired | 永久終態，只供稽核 | storage |

`paused` 是 **Turn** 暫停，不是 bot 沉睡；`suspended` 是 Turn 已做 durable checkpoint、可跨 activation 恢復。bot 可以 dormant 且帶一個 suspended Turn。

## 身分必須分三層

```text
agent_path       /root/research/web；組織位置，可讀名稱
incarnation_id   這個 bot 從建立到 retire/recreate 的穩定身分
activation_id    每次 wake/materialize 都新建；worker lease 的 fencing token
```

睡醒不換 `incarnation_id`，所以 task、mailbox、history 仍屬同一個 bot；但一定換 `activation_id`，舊 worker 即使晚回來也不能提交到新活動。這表示既有 `agent_runtime/PLAN.md` 的 `instance_id` 應視為 incarnation，另補 activation/lease id，而不是每次喚醒都重建 team member。

每個 bot record 至少保存：immutable system/tool/model refs、history head＋generation、effective policy ref、idle/wake policy、incarnation、effective state、current/suspended Turn、last checkpoint、fault reason。events append-only；snapshot 只是 projection。

## 誰能喚醒

所有來源先變成 durable `WakeIntent`，不能直接 spawn process：

1. operator 或有 `wake_descendant` grant 的 leader 明確要求。
2. `assign_task(..., wake="run")` 完成 task／資源 transaction 後要求；這是 leader 委派的首選。
3. standby policy 允許的事件：新 assigned task、主管訊息、child terminal report、timer/dependency。
4. 預先登記的 alarm；它是明確承諾，dormant 也可被喚醒。

普通 `send_message` 只保證入 mailbox，**預設不等於 wake**。standby bot 只有在寄件者、事件種類、team scope 都符合 policy 時才自動產生 intent。bot 自己不輪詢 mailbox；supervisor 的 event index 做匹配。

多個同時事件要依 `operation_id` 去重，並按 bot／history branch 合併成一次 wake。WakeIntent 保存 actor path＋incarnation、cause refs（task/message/report）、reason、deadline、policy-derived priority 與建立時間；不把任意模型文字當 scheduler 權限。

## leader 喚醒一個 dormant 下屬

推薦完整流程：

```text
leader assign_task(wake=run)
 -> team 驗 actor/grant/scope，reserve task budget，落 task record
 -> control plane 寫 WakeIntent + event（同一 operation id）
 -> scheduler CAS dormant/standby -> queued；合併其他 pending causes
 -> hydrate 並驗 system/tools/history/policy/version
 -> 建 activation_id，編譯有界 input manifest
 -> 取得 endpoint lease，開始或恢復一個 Turn
 -> Round / tool 依各自 queue 執行
 -> report + checkpoint + idle disposition
 -> 通知 leader；必要時依其 standby policy 喚醒 leader
```

task record 是事實來源；notification 掉了仍可查 task。原因只有在 input manifest 原子提交後才算 delivered，crash 前可重送但不可遺失。沒有 capacity 只是繼續 queued，不是 fault；hydrate 驗證失敗才進 faulted 並通知主管。

直接 `wake_agent` 適合叫醒已有 inbox／suspended Turn 的 bot。它不能替代 `assign_task` 偷渡一個沒有 task、grant、budget 的正式委派。leader 只能提出 wake request，不能指定 worker、raw process argv 或繞過 endpoint scheduler。

## 如何沉睡或待命

自然完成 Turn 後，supervisor 按 disposition 決定：有可執行原因就 queue 下一 Turn；否則進 standby 或 dormant。standby 只是保留 wake policy，沒有活 process。

leader 或 bot 自己要求 sleep 時：

1. 寫 `SleepIntent`，狀態改 draining；拒絕新的 Round／tool batch／Turn。
2. 已送出的 Round 收完整 response；已開始的 tool batch 完整跑完並 settle，不能留下半包 tool results。
3. 保存 history head、usage、task/report、pending causes 與 Turn continuation，再原子提交 checkpoint。
4. Turn 可 terminal，或以 `suspended` 保留；釋放 endpoint、tool、process、capacity lease，bot 進 dormant。

睡眠不是取消已發生的副作用。Round 回來的新 tool calls 若尚未開始，不應隔幾小時後盲目執行；checkpoint 要標 `needs_revalidation`，wake 時預設回覆「未因 sleep 執行」並讓模型依新現場 replan。只有明確 policy 才可原樣 continue。

pause 不做以上 dematerialize；適合短暫人工 steering。長期不工作應 sleep，否則大量 paused Handle 仍成為常駐 runtime state。

leader 尚有 active descendants／未收 child reports 時，預設只能 standby，讓 `child_terminal` 可喚醒它；要求 dormant 必須指定 escalation owner 或 `force` 權限，避免整棵 team 無人收結果。

## wake 時重新驗證

wake 不是反序列化後直接續跑。每次 activation 都重新檢查：incarnation 尚有效、actor grant、task deadline/budget、engine/tool policy、workspace generation、history/tool pairing、suspended continuation 與 artifact refs。權限撤銷後只能縮小；不足就 blocked/faulted，不沿用舊 activation 的權限。

consumable task budget 可在睡眠中保留；endpoint/CPU/RAM/PID/concurrency capacity 一律釋放。deadline 是 wall clock，睡眠不會偷偷暫停，除非 task 明訂 active-time budget。

配套 tools、endpoint cache-aware scheduling、失敗恢復與驗收見 [`BOT-LIFECYCLE-OPERATIONS.md`](BOT-LIFECYCLE-OPERATIONS.md)。這些是 agentloop 審核後的 control-plane 規格輸入；現在不修改 agentloop、team/runtime 實作，也不先做 HTTP/9P。
