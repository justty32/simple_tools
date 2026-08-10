# 多 agent 實作路線

這份只排跨 package 的依賴與驗收順序；各層資料模型和關卡見自己的 `PLAN.md`。所有新規劃檔維持 8 KB 以下。

Plan 9、Linux-as-Lisp、九軸與組織化記憶的綜合底圖見 [Agent World 設計報告](../docs/agent-world/README.md)。

## 依賴圖

```text
agent_identity ─┬─► communication_tools ─┐
                ├─► agent_runtime ───────┼─► team_tools
                └────────────────────────┘

agentloop Turn/Round ─► agent_runtime
memory_tools ─────────► communication/team artifacts
introspection_tools ──► 唯讀聚合所有 effective state
agentfs ──────────────► 將 effective state 投影成 synthetic filesystem
organized context ────► memory object/ref + Round manifest + agentfs view
```

`agent_identity.py` 不是模型工具，只是一份大家共用的 logical path parser。不要讓 communication、runtime、team 各寫一版 ancestry。

## Milestone 0：語意與純資料模型

先做完全不需模型、Podman或網路的部分：

1. `agent_identity.py`：parse、canonicalize、parent、child、ancestor、segments。
2. Turn/Round record：`turn_id`、round counter、phase 與 stop reason。
3. permission/resource value objects：subset、downgrade、reserve request。

Gate：`/root/a` 不得是 `/root/ab` 的 ancestor；所有 object 可 serialize；未知欄位 fail closed；沒有任何模型輸入能產生 raw filesystem/runtime argv。

## Milestone 1：communication vertical slice

完成一個 supervisor provision 兩個 endpoint、互寄、check、read、archive 的離線流程。

Gate：100 封並發不遺失、sender 不可偽造、壞信隔離、路徑／symlink 不越界、沒有阻塞 wait。此時還沒有 team。

## Milestone 2：Turn control

在保持現有普通工具相容下，加入：

- 回合內 thread-safe additional-instruction queue。
- completion/enqueue 邊界 lock。
- correlated tool-input request/response、timeout、cancel。
- `Handle.now()` 的 `awaiting_tool_input`。

Gate：最後一輪競態不丟指令；工具輸入不進 bot history、不增加 Round；停止能喚醒 blocked tool；原本 agentloop 離線關卡全過。

## Milestone 3：runtime fake backend

先不用容器，以 fake child 驗證 policy derivation、budget reserve/refund、instance lifecycle 與 collect idempotency。再接 rootless Podman backend，落實每 agent instance 的 mount、network 與 cgroup policy。

Gate：child 不能升權或超賣、失敗退款一次、同 path active instance 唯一、無 Podman socket、cleanup 有 evidence。

## Milestone 4：memory 與 introspection

memory 先做 address/store，再接 bot history；introspection 只讀真正 effective state，不讀 request policy。

Gate：JSON `$ref`、Markdown link/heading、權限、cycle/bytes 限制全部離線驗；卸載不破壞 tool-call 配對；無 sandbox 時 status 不得聲稱已隔離。

一般化的段落提升與自動 context compiler 延後：先保存不可變 source trace 和每輪 manifest，再逐步開放 assistant research/report 的人工 promote；當前 Turn 指令永遠 inline。

## Milestone 5：team state machine

依序完成 member/grant/allocation、task/report、communication notification，最後才做 `spawn_subordinate` saga。

Gate：同儕與跨 subtree 越權被擋、權限單調縮減、資源守恆、task transition 合法且冪等；通知失敗不會讓 task state 消失；spawn 中途失敗可完整補償。

## Milestone 6：agentfs projection

先以 `agent_list`／`agent_read` 驗證 `/root/.../.agent/` namespace、provider 與 per-actor view；不要一開始就把 FUSE mount 問題和資料模型綁在一起。resolver 穩定後，再依部署需求選 FUSE 或 9P adapter。

Gate：self／ancestor／peer 視圖正確、secret 永不出現、snapshot generation 一致、所有 projection read-only；若加入 `ctl`，必須走既有 typed operation 與相同 audit。

## 第一個可動手的切片

從 `agent_identity.py` 開始，因為它小、純函式、同時卡住 communication/runtime/team 三層。建議第一批 API：

```python
parse_agent_path(text) -> AgentPath
AgentPath.parent
AgentPath.child(segment)
AgentPath.is_ancestor_of(other)
AgentPath.segments
str(AgentPath)          # canonical /root/...
```

先只接受 `/root` 與其 descendants；不做相對 path、reparent、alias 或 wildcard。這批驗完，再進 communication mailbox，能避免後面三套路徑規則漂移。
