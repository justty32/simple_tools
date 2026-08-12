# FreePy 未來規格

本目錄全部是尚未完成或刻意延後的規格，不是可 import package，也不代表近期一定實作。

| 主題 | 入口 |
|---|---|
| durable domain kernel | [Agent Machine](agent-machine/README.md) |
| 大量 dormant bots | [control plane](control-plane/README.md) |
| OS worker / sandbox | [agent runtime](agent-runtime/PLAN.md) |
| namespace projection | [agentfs](agentfs/PLAN.md) |
| context / memory | [memory tools](memory-tools/PLAN.md) |
| organization | [team tools](team-tools/PLAN.md) |
| durable messages | [communication tools](communication-tools/PLAN.md) |
| LLM-assisted tool description | [exec tools](exec-tools/PLAN.md) |
| effective-state view | [introspection tools](introspection-tools/PLAN.md) |

實作順序仍以 [`freepy/ROADMAP.md`](../../../freepy/ROADMAP.md) 為準。目前 durable data boundary
暫緩；這些文件用來保存問題與候選方案，不可覆蓋 package README 所描述的現況。
