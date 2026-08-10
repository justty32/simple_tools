# IDEAS／PLAN 中的規劃評估

| 規劃 | 等級 | Janet 負責 | 外部邊界 |
|---|---:|---|---|
| `agent_identity` | A | canonical path、parent、ancestor、segments | 無 |
| communication | A/B | mailbox schema、排序、狀態、ACL decision | durable fs／DB 原子性 |
| team | A/B | org tree、grant、allocation、task/report state machine | runtime reserve、通知 backend |
| runtime | B/C | policy derivation、budget ledger、lifecycle records | Podman/cgroup/seccomp/process API |
| memory | A/B | address、resolver、manifest、provenance、budget | object store／search adapter |
| introspection | A/B | effective snapshot、read-only views | 各 backend 的真實 telemetry |
| agentfs | B/C | resolver、provider、per-actor projection | FUSE/9P/native mount |
| Round control | B | queue、correlation、phase transition | blocking tool/user I/O |

## 最自然的 Lisp 核心

`agent_identity`、permission subset、resource arithmetic、task transition、memory address、context manifest 都是「資料 + 不變條件 + reduction」。它們不需要 class hierarchy；用 Janet table/struct、純函式與 pattern/PEG 更直接。

這也呼應既有 Linux-as-Lisp 對應：

```text
path      = symbol / stable name
namespace = evaluation environment
mount     = capability binding
handle    = resolved reference + authority
agent     = evaluator with bounded lifecycle
Round      = bounded evaluation activity
Step     = ask → message reduction
memory    = quoted object; load = checked dereference
```

## 各層的真正邊界

### Communication／team

Janet 很適合 message、member、grant、task、event 的 typed state。可是「寫入只有完整可見或不可見」、跨 process lock、crash recovery、SQLite/Redis durability 不是 Lisp 性質；它們要由 storage adapter 保證。

### Agent runtime

policy 的集合運算、child 只能縮權、reserve/refund 冪等都適合 Janet。真正執行限制必須由 trusted supervisor 轉成 Podman/cgroup/seccomp/mount 設定。受限 agent 不得拿 container socket，也不能讓 Lisp core 直接輸出未驗證 raw argv。

### Memory／organized context

這一層非常 Lisp：source trace 不變，span 提升成 immutable object，context 是每個 Step 重新編譯的 working set。quote/dereference 是好模型。

但 `marshal` 不能成為持久協定。它可做暫存快照，正式格式仍應是版本化 JSON/bytes + content hash，才能跨語言、稽核與遷移。

### Agentfs

provider、walk/list/stat/read、snapshot generation、per-actor view 可先在 Janet 內部實作。不要一開始做 mount；先用 `agent_list`／`agent_read` 驗證 namespace 語意。FUSE/9P 等 resolver 穩定後再選 C module 或外部 server。

## Plan 9／Linux 類比的限制

path 不能同時是 identity、authority、command。namespace 是最小暴露面，不是唯一安全邊界；server 每次 read/open 仍要驗 grant。從 memory 載入的文字是帶 provenance 的 data，不能因內容像指令便提升權限。

這些限制恰好是固化時應寫成純規則與 fixture 的部分，而不是只留在文件裡。

## 依賴順序不能跳

```text
identity → communication/runtime → team
Round/Step → runtime
memory + introspection → agentfs projection
source trace + memory → context compiler
```

即使 team state machine 很適合 Lisp，也不能先於 identity、reserve 與 runtime enforcement 完成，否則只有漂亮的帳本，沒有真正限制。

## 來源

- [ROADMAP](../../freepy/ROADMAP.md)
- [agent runtime](../../freepy/agent_runtime/PLAN.md)
- [team tools](../../freepy/team_tools/PLAN.md)
- [memory tools](../../freepy/memory_tools/PLAN.md)
- [organized context](../../freepy/memory_tools/ORGANIZED-CONTEXT.md)
- [agentfs](../../freepy/agentfs/PLAN.md)
- [Linux-as-Lisp](../../freepy/agentfs/LINUX-AS-LISP.md)
