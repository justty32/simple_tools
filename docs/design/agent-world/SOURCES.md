# 來源與可信度

本報告刻意區分「正式規範」、「尚未收束的思考」與「已跑過的原型」。吸收概念不代表原樣照搬；衝突處以本報告的明示結論為準。

## `ai_core` 正式／較高可信來源

根目錄：`C:/code/mine/ai_core`

| 路徑 | 本報告採用的內容 |
|---|---|
| `workflows/spec/execution_forms/s0-axes.md` | 九個概念軸與 trigger 正交性 |
| `workflows/spec/axis_spec/` | lifecycle/state/resources/interruptible/guarantee/nondeterminism 的理由 |
| `workflows/spec/lib_spec/` | `entries` 等現行 metadata fields、證書與 reliability |
| `workflows/spec/composite_spec/` | `.config/.cache/.state/.data` 與 recovery convention |
| `workflows/spec/cli_spec.md` | argv 可視為 Lisp-style list/keyword pairs |
| `sub_projs/handy/agent-base.md` | 一個資料夾一個 agent、一次性核心、上下文外部化 |
| `sub_projs/handy/llm-nature.md` | LLM 隨機性、腳手架門檻、固化與證書 |

注意：`axis_spec/README.md` 自己標示仍在 Phase 1；`lib_spec` 是更具體的現行契約，但 repo 內也有 `handy` 的 metainfo 支線。報告只借九軸的正交問題與 declared/effective/observed 分層，不宣稱兩條旗標支線已合併。

## `ai_core` 思考來源

以下是高價值但可能自我修正的 notes：

| 路徑 | 關鍵想法 |
|---|---|
| `workflows/notes/20260719-1150-世界-os-as-game-tick-npc-檔案系統即場景.md` | tick、巢狀世界、快慢世界、檔案作通用協議 |
| `workflows/notes/20260719-1301-兩層底座重解-歸一於路徑-tick即時間轉換.md` | 先歸一於路徑、後成局；tick 是狀態轉移 |
| `workflows/notes/20260718-2324-so與腳本共存-呼叫閘道-value跨邊界.md` | exec 地板、in-process 快路、Value 兩種物理形態 |
| `workflows/notes/20260708-1040-一切皆檔案-list-統一描述子-複合路徑.md` | inode/cons、統一描述子、caller-side metadata composition |
| `sub_projs/handy/notes/2026-07-26.md` | context 不該無限塞入；要營造簡單世界模型 |
| `sub_projs/handy/notes/2026-07-27-design.md` | agent 是 team、外包 vs 換手、subagent 回報壓縮事實 |
| `sub_projs/handy/notes/2026-07-28-lisp.md` | os-as-lisp、homoiconicity、cons cell 換 inode |
| `sub_projs/handy/notes/2026-07-28-analyzable.md` | facts、固定點推論、機械事實與模型猜測分層 |
| `sub_projs/handy/notes/2026-07-28-intent.md` | 意圖是較長推論鏈；grounding、審核與依賴失效 |
| `sub_projs/handy/notes/2026-07-28-prototype.md` | 唯讀原型、LLM 呼叫留 trace、先取樣再固化 |
| `sub_projs/handy/notes/2026-08-01.md` | agent 活躍／沉寂記憶是同一本體 |
| `sub_projs/handy/notes/2026-08-02.md` | 程式作人的本體論；記憶、下屬、方式、資源 |

這些 notes 明寫會保留矛盾。例如「機械事實不得依賴 LLM 猜測」後來被修成「猜測先成 hypothesis，經審核 grounding 後才可作前提」；本報告採後者。

## 原型實證

`C:/code/mine/ai_core/sub_projs/unipath/OVERVIEW.md` 記錄的 step 1–6 已完成：FUSE live tree、跨 process、真 9P2000、Linux v9fs mount、Python/C++/Fennel 互通、tick、Janet script nodes。

它證明：

- live process state 可被 path walk/read/write。
- `ctl/data/status` file protocol 能跨語言。
- 9P 可作 transport，並非純比喻。

它沒有證明 production ACL、per-actor namespace、secret handling、revocation、agent lifecycle 與 Windows adapter；本報告不把這些視為已完成。

## 本 repo 既有規劃

- `docs/freepy/future/agentfs/PLAN.md`
- `docs/freepy/future/agentfs/LINUX-AS-LISP.md`
- `docs/freepy/future/memory-tools/PLAN.md`
- `docs/freepy/future/memory-tools/ORGANIZED-CONTEXT.md`
- `freepy/agentloop/ROUNDS.md`
- `docs/research/sandboxing/`

本報告是跨層綜合；實作順序以 `freepy/ROADMAP.md` 為準。`future/` 下的
`PLAN.md` 是延後候選規格，不是目前排程。

## 外部一手資料

- [Plan 9 name spaces](https://9p.io/sys/doc/names.html)
- [Plan 9 system overview](https://9p.io/sys/doc/9.html)
- [Plan 9 9P server API](https://9p.io/magic/man2html/2/9p)
- [Plan 9 bind/mount and `/srv`](https://9p.io/magic/man2html/1/bind)
- [Plan 9 Factotum](https://9p.io/sources/plan9/sys/man/4/factotum)
- [Plan 9 plumbing rules](https://9p.io/magic/man2html/6/plumb)
- [Venti content-addressed archival storage](https://9p.io/sys/doc/venti/venti.pdf)
- [Linux VFS](https://docs.kernel.org/filesystems/vfs.html)
- [Linux `/proc`](https://docs.kernel.org/filesystems/proc.html)
- [Linux sysfs](https://docs.kernel.org/filesystems/sysfs.html)
- [Linux configfs](https://docs.kernel.org/filesystems/configfs.html)
- [Linux namespaces](https://man7.org/linux/man-pages/man7/namespaces.7.html)
- [Linux mount namespaces](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html)
- [Linux cgroup v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)
- [Linux seccomp BPF](https://docs.kernel.org/userspace-api/seccomp_filter.html)

## 本報告的推論

以下是來源啟發後的新綜合，不宣稱來源原文已定案：

- canonical agent identity 與 per-agent mounted view 要分開。
- path 是 symbol；session handle/fid 才是 capability。
- Step、Round、tick 是三個不同時間尺度。
- 九軸應同時用於 agent/tool 與 memory/context transforms。
- organized memory 應包含可撤回 grounding 與 dependency invalidation。
- context manifest 是 replay 的真相；compact 是重編譯 working set。
