# 規劃中模組的 C／C++ 評估

這一頁只評架構 fit。下列模組目前是 `PLAN.md`，不是等待翻譯的 Python implementation。

| 規劃 | C++ 評級 | C++ 適合擁有 | 外部／高風險邊界 |
|---|---:|---|---|
| `agent_identity` | A | canonical path、segments、parent/ancestor | 無 |
| communication | A/B | envelope、ACL decision、ordering、file/SQLite adapter | fsync、lock、crash recovery |
| team | A/B | grant、allocation、task/report state machine | runtime reserve、durable transaction、通知 |
| runtime | B | policy derivation、ledger、lifecycle；Linux supervisor 可 native | Podman/cgroup/seccomp/mount、credentials |
| memory | A/B | address、resolver、manifest、provenance、limits | object store、search、large bytes |
| introspection | A/B | immutable snapshot／view projection | backend telemetry truth |
| agentfs | B | resolver、provider、per-actor view、snapshot | FUSE/9P session、kernel mount |
| Round control | B | queue、correlation、phase、cancel state | blocked tool/user I/O、scheduler |

## 比 Janet 更能接 effect，不代表可以混層

C++ 有成熟的 TLS、SSE、SQLite、process、FUSE 與 OS API 路徑，所以 native owner 可以比 Janet 方案向下延伸。但仍要維持：

```text
policy/state decision → typed intent → privileged adapter → result event
```

原因不是語言能力，而是權限與可重播性。C++ core 若同時拿 raw model input、Podman socket、host root 與 secret，會比 Python 版本更難隔離；「編成 binary」不會自動縮小 authority。

## 最自然的 C++ value core

- `AgentPath`：canonical parse、保留 `.agent`、segment-safe ancestry。
- `PermissionSet`：subset／downgrade，不接受 raw runtime argv。
- `BudgetLedger`：reserve／commit／refund、idempotency key、守恆。
- `RoundState`：Step、pending debt、phase、stop reason、usage。
- `TaskState`：合法 transition、immutable report、notification intent。
- `MemoryTarget`：scheme、workspace-relative path、fragment、ACL context。
- `AgentView`：actor + instance + grant generation 綁定的 read-only projection。

這些應是 movable value、明確 ownership、可 serialize 的資料；不要在 core 放 global singleton、裸 pointer graph 或 hidden thread。

## Communication／team

file mailbox 第一版可由 C++ 直接實作 atomic temp-write + rename；若進多 process、reserve 與 task transaction，SQLite 的 C API 也很適合 native adapter。可是 durable truth 要選一個 owner：不能 JSON snapshot、SQLite 與 event log 三邊各自成功一半。

所有 mutation 都經 operation id；通知失敗不回滾 authoritative task，但應產可重試 intent。C++ 的 RAII 只能幫資源 cleanup，不能替你決定 saga／idempotency 語意。

## Runtime

C++ 很適合做 Linux-first supervisor：fork/exec 或 container runtime client、pipe drain、signal、deadline、cgroup telemetry 與 cleanup。這是相對 Janet 最明顯的 native 收益。

但模型永遠只送 typed spawn request。supervisor 自己從綁定的 actor、effective policy 與允許的 template 重建 argv／mount／network 設定；不執行模型提供的 raw command line。受限 child 不得拿 runtime control socket。

Windows 需要獨立 backend 與 Job Object／process tree／UTF-16 argv 測試，不應從 MinGW build 成功推論已支援 production runtime。

## Memory／organized context

content-addressed bytes、hash、JSON Pointer、Markdown link/heading、cycle/depth/bytes budget 都適合 native parser/resolver。模型摘要、semantic search、embedding provider 則是 adapter。

source trace、memory object 與每步 manifest 要分開；C++ object layout 或 binary archive不能成為持久 ABI。正式格式仍是版本化 JSON/NDJSON/bytes + hash，否則升 compiler／stdlib 就失去可重播性。

## Introspection／agentfs

兩者應共用 immutable effective-state snapshot。agentfs 先做 `list/read/stat` provider，之後再掛 FUSE/9P。native 實作能降低 mount adapter 摩擦，但也把 parser、path、permission 與 kernel callback 放進同一 TCB；必須 fuzz path、限制 bytes/list、每次 read 驗 actor/instance/grant generation。

## 依賴順序

```text
identity + common values
  ├─ communication ─┐
  ├─ runtime ───────┼─ team
  └─ Round/Step ────┘

memory + effective snapshots → introspection → agentfs projection → mount adapter
```

不要因 C++ 適合寫 team state machine就跳過 identity、ledger 與 runtime enforcement。漂亮的 native 帳本若沒有原子 reserve 與 OS ceiling，仍然只是記錄願望。

## 來源

- [ROADMAP](../../freepy/ROADMAP.md)
- [agent runtime](../../freepy/agent_runtime/PLAN.md)
- [communication](../../freepy/communication_tools/PLAN.md)
- [team](../../freepy/team_tools/PLAN.md)
- [memory](../../freepy/memory_tools/PLAN.md)
- [agentfs](../../freepy/agentfs/PLAN.md)
