# 規劃來源與取捨

本規劃於 2026-08-13 盤點 `docs/` 132 個檔案與 `freepy/` 131 個受版控文字檔後整理；HTML/CSS
視為導覽衍生物，程式、checks、package README、canonical architecture 與 Roadmap 依序優先。
`freepy/.venv`、`__pycache__` 等生成物不作設計輸入。

## 本 repo 的主要依據

- 原始構想：[`../../../freepy/notes/agent-machine.md`](../../../freepy/notes/agent-machine.md)。採用
  LLM/tool 是異質 executor、VFS 是統一位址空間、`name/.name`、refs/GC、Git 與內建自我改進。
- 現況邊界：[`../architecture.md`](../architecture.md)、
  [`../../../freepy/ROADMAP.md`](../../../freepy/ROADMAP.md)。保留 local `agentloop` 與 durable outer
  machine 的分層，不把 DB/lease 術語塞回 Handle。
- 時間與安全邊界：[`../../../freepy/agentloop/ROUNDS.md`](../../../freepy/agentloop/ROUNDS.md)、
  [`../../../freepy/agentloop/RUNNER.md`](../../../freepy/agentloop/RUNNER.md)。沿用 Round/Step、
  tool pairing、single owner 與 cooperative pause/end。
- 舊 Agent Machine 規格：[`../future/agent-machine/`](../future/agent-machine/README.md)。採用
  proposal/validation/commit、generation、resource ledger、fair scheduling、evidence 與 recovery；
  更新實作先後與 VFS/Git 地位。
- Namespace/memory：[`../../design/agent-world/`](../../design/agent-world/README.md)、
  [`../future/agentfs/PLAN.md`](../future/agentfs/PLAN.md)、
  [`../future/memory-tools/PLAN.md`](../future/memory-tools/PLAN.md)。採用 path/identity 分離、per-actor
  view、typed control、ordered context manifest、ACL 與 load limits。
- Runtime/security：[`../future/agent-runtime/PLAN.md`](../future/agent-runtime/PLAN.md)、
  [`../../research/sandboxing/README.md`](../../research/sandboxing/README.md)。不把 path containment、
  callback 或 agentfs 說成 sandbox；Linux enforcement 延後但 fail-closed 語意先定。
- 語言評估：[`../../research/languages/cpp/`](../../research/languages/cpp/README.md)、
  [`../../research/languages/janet/`](../../research/languages/janet/README.md)。採用 contract-first、
  differential fixtures、C ABI、single-owner event queue、Janet pure policy 與 Linux-first native tests。
- 框架研究：[`../../research/agent-frameworks/`](../../research/agent-frameworks/README.md)。只借用
  checkpoint、interrupt/resume、typed output 和 trace，不引入大框架作核心。

## 外部 repo 盤點

使用者允許的四個 repo 先做全樹檔名/關鍵字篩選，再精讀直接相關檔案：

| Repo | 掃描檔案數 | 直接採用的證據 |
|---|---:|---|
| `C:/code/mine/paper_readings` | 2,252 | 大量 dormant agent、控制面/worker/effect 三種生命週期、機率評分限制 |
| `C:/code/mine/machine_learning` | 302 | 既有研究脈絡；沒有比本 repo canonical contract 更直接的可搬實作 |
| `C:/code/mine/ai_core` | 1,639 | file inbox atomic rename、外部化 context、recovery manifest、Unipath provider/9P 分層 |
| `C:/code/mine/langlab-janet` | 149 | Janet embed/native module、LLM/tool loop、fiber/thread/channel、build 與 Windows 地雷 |

最直接的外部路徑：

- `ai_core/sub_projs/handy/agent-base.md`：folder-as-agent、one-shot run、mailbox rename、budget。
- `ai_core/sub_projs/unipath/OVERVIEW.md`、`ports/cpp/`：先定 Env/provider，再接 9P；codec、dispatcher、
  state engine 分離。其 detached-thread demo 不直接搬進 production。
- `ai_core/sub_projs/workflows/spec/composite_spec/state-dir-convention.md` 與
  `interruption-recovery.md`：config/state/data/cache 分義、in-progress/done 與 resume/rollback manifest。
- `langlab-janet/examples/embed/embed.c`、`examples/native-module/`、`docs/10d-native-與嵌入.md`：
  libjanet embed 與 native cfun 的實際 build/API。
- `langlab-janet/docs/15-ev-channel-net.md`、`FINDINGS-踩坑.md`：單執行緒 fiber、跨 thread marshal、
  cooperative cancel、jpm/DLL/subprocess 限制。
- `paper_readings/deep/agent_scale_fault_cognition/`：executor slot、lease、external effect 與 self-improvement
  branch/eval gate；支持 scheduler 不依賴模型自述信心。

這些路徑是本機研究來源，不成為 simple_tools 的 build dependency，也不複製其程式碼。

## EvoMap / GEP

另行查閱 EvoMap 官方的 [GEP Protocol](https://evomap.ai/wiki/16-gep-protocol)、
[Evolver repo](https://github.com/EvoMap/evolver) 與
[SKILL.md](https://github.com/EvoMap/evolver/blob/main/SKILL.md)。其 append-only event、content id、
策略／執行結果分離、假設與本機驗證流程納入 [07-evolution.md](07-evolution.md)；遠端 Hub、Node runtime、
自動 publish 和破壞性 Git rollback 不成為核心依賴。

## 已解決的文件張力

| 張力 | 本規劃的決定 |
|---|---|
| 「一切皆檔案」vs typed authority | 檔案是介面；控制 write 轉 typed operation，核心驗證後才 commit |
| Function 就是檔案 vs `/proc` 只是 view | 不用 `/proc`；`name/.name` 是公開 representation，live worker 仍是短生命實例 |
| path 是指標 vs rename/ACL | path 負責查找；stable node id + generation + rights handle 負責身分與授權 |
| filesystem 是真源 vs append-only event | current VFS generation 是正式狀態；journal 是 recovery/audit，不另開 mutable truth |
| Git 是基礎 vs transaction ledger | Git 管 versioned checkpoint/branch；VFS transaction、usage ledger、外部 effect 另有契約 |
| `.refs` 決定垃圾 vs active race/history | `.refs` 加 lease/pin/system/Git roots；snapshot mark、quarantine、grace 後 purge |
| Round 是函數 vs 現有用語 | Function 是定義；Round 是一次函數呼叫；Step 是一條 LLM instruction |
| Janet core vs C++ core | C++ 唯一提交；Janet 做純 policy，回 decision 後仍由 C++ 驗證 |

## 尚未當成事實的部分

Content-addressed directory backend、SQLite index、commit-loop throughput、Git checkpoint 粒度、Janet
policy process/env 數量與 native LLM transport 都要靠原型量測。它們是候選實作，不在文件中假裝
已經完成或最佳。
