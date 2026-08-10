# 想法收件匣

**只放還沒分類、還沒決定要不要做的東西。** 想到就先丟進來；一旦開始設計，就搬到對應的
`PLAN.md`。已經落地且需要解釋「為什麼」的決定才進 [NOTES.md](NOTES.md)。

## 目前分類

跨 package 的依賴、里程碑與第一個實作切片集中在 [ROADMAP.md](ROADMAP.md)；跨 package 的
思想綜合與 HTML 導覽見 [Agent World 設計報告](../docs/agent-world/README.md)。

| 原想法 | 現在的位置 | 分類理由 |
|---|---|---|
| agent runtime 如同 OS；慢速機率 Lisp machine | [agent_machine/](agent_machine/README.md) | 已成為 resource scheduler、goal verifier 與持久狀態的上層實作規格 |
| agent 之間寄信、查信、廣播 | [communication_tools/PLAN.md](communication_tools/PLAN.md) | 這只是 transport，不是團隊 |
| 有上下級的多 agent 集合 | [team_tools/PLAN.md](team_tools/PLAN.md) | 組織、授權、資源、任務與回報 |
| 產出 child agent、針對性 fork | [agent_runtime/PLAN.md](agent_runtime/PLAN.md) | 涉及 lifecycle、sandbox、權限與預算守恆 |
| `$ref` 卸載／載入 | [memory_tools/PLAN.md](memory_tools/PLAN.md) | 結構化記憶；同時支援 JSON `$ref` 與 Markdown link |
| context 段落提升成檔案、原位留連結 | [memory_tools/ORGANIZED-CONTEXT.md](memory_tools/ORGANIZED-CONTEXT.md) | 不可變 trace、組織化 memory 與按步編譯的 working set |
| 我是誰、在哪、還剩多少 | [introspection_tools/PLAN.md](introspection_tools/PLAN.md) | 唯讀觀察，不屬於 runtime mutation |
| 用檔案路徑存取 live agent state | [agentfs/PLAN.md](agentfs/PLAN.md) | Plan 9／procfs 式的 synthetic filesystem projection |
| 用 Linux-as-Lisp 理解 agent namespace | [agentfs/LINUX-AS-LISP.md](agentfs/LINUX-AS-LISP.md) | namespace 是環境、mount 是綁定、fd 是能力引用 |
| 回合、步、追加指令、工具輸入 | [agentloop/ROUNDS.md](agentloop/ROUNDS.md) | agentloop 的活動與互動語意 |

## 已固定但尚未實作的關鍵決定

- Agent 是持久 identity／namespace／image；active Round 是邏輯 process；Step 是 stochastic quantum。
- endpoint 回傳、agent 停止、attempt 結束與 goal achieved 是不同事件；只有 verifier 能提交 achieved。
- Linux 管 process/cgroup/namespace 等 physical resources；userspace Agent Machine 管 endpoint、token、
  context、goal 與 tool effect；目前不做 kernel module。
- canonical agent identity 用 Unix-like path；path 是組織位置，不自動創造權限。
- 每個 agent 有 supervisor 組成的 Plan 9 式私有 namespace；agentfs 只是 effective state projection。
- child 權限只能縮小；可消耗預算要先 reserve，不能複製；resource settlement 必須冪等。
- 一個 Round 從模型啟動到主動停止；一次 `ask() → message` 是一個 Step，工具在兩步之間執行。
- memory link 不是授權；source trace 不因 compact 改寫；每個 Step 保存實際 context manifest。

## 尚未分類的新想法

目前沒有。新東西先加在這裡，不要直接塞進最像的 package；先問它是在做：

1. 定義 Agent Machine 的求值、goal 或資源語意；
2. 傳輸資料；
3. 管理組織與工作；
4. 建立／限制 execution；
5. 管理 context／memory；
6. 觀察 effective state；
7. 還是修改 agentloop 本身。
