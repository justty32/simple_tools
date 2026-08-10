# 想法收件匣

**只放還沒分類、還沒決定要不要做的東西。** 想到就先丟進來；一旦開始設計，就搬到對應的 `PLAN.md`。已經落地且需要解釋「為什麼」的決定才進 [NOTES.md](NOTES.md)。

## 目前分類

2026-08-10 已把原本三組想法搬成可實作規格：

跨 package 的依賴、里程碑與第一個實作切片集中在 [ROADMAP.md](ROADMAP.md)。

跨 package 的思想綜合與 HTML 導覽見 [Agent World 設計報告](../docs/agent-world/README.md)。

| 原想法 | 現在的位置 | 分類理由 |
|---|---|---|
| agent 之間寄信、查信、廣播 | [communication_tools/PLAN.md](communication_tools/PLAN.md) | 這只是 transport，不是團隊 |
| 有上下級的多 agent 集合 | [team_tools/PLAN.md](team_tools/PLAN.md) | 組織、授權、資源、任務與回報 |
| 產出 child agent、針對性 fork | [agent_runtime/PLAN.md](agent_runtime/PLAN.md) | 涉及 lifecycle、sandbox、權限與預算守恆 |
| `$ref` 卸載／載入 | [memory_tools/PLAN.md](memory_tools/PLAN.md) | 結構化記憶；同時支援 JSON `$ref` 與 Markdown link |
| context 段落提升成檔案、原位留連結 | [memory_tools/ORGANIZED-CONTEXT.md](memory_tools/ORGANIZED-CONTEXT.md) | 不可變 trace、組織化 memory 與按輪編譯的 working set |
| 我是誰、在哪、還剩多少 | [introspection_tools/PLAN.md](introspection_tools/PLAN.md) | 唯讀觀察，不屬於 runtime mutation |
| 用檔案路徑存取 live agent state | [agentfs/PLAN.md](agentfs/PLAN.md) | Plan 9／procfs 式的 synthetic filesystem projection |
| 用 Linux-as-Lisp 理解 agent namespace | [agentfs/LINUX-AS-LISP.md](agentfs/LINUX-AS-LISP.md) | namespace 是環境、mount 是綁定、fd 是能力引用 |
| 回合、輪、追加指令、工具輸入 | [agentloop/TURNS.md](agentloop/TURNS.md) | agentloop 的活動與互動語意 |

依賴方向：

```text
team_tools ──► communication_tools
     │
     ├──────► agent_runtime ──► agentloop
     │                              │
     └──────► memory_tools          └── Turn / Round

introspection_tools 唯讀聚合上述各層的 effective state
agentfs 將同一批 effective state 投影成受權限控制的檔案 namespace
```

## 已固定但尚未實作的關鍵決定

- canonical agent identity 用 Unix-like path，例如 `/root/leader/worker`；父路徑是直接主管，subtree 是組織 scope。
- agent path 是邏輯 namespace，不是宿主 filesystem path；各層共用 `agent_identity.py`。
- agent path 下保留 `.agent/` 作為 live filesystem 介面；子 agent 仍直接構成組織樹。
- `agentfs` 的理想模型明確採 Plan 9：每個 agent 是 file server，agent group 是可掛載、可裁切的 namespace；FUSE/9P 是後端，不是核心契約。
- path 表示組織位置，不自動創造權限；grant 仍由 supervisor 驗證。
- child 權限只能是父 effective permissions 的子集；可消耗預算要先 reserve，不能複製。
- `fork` 是 spawn 的 `context="fork"` 模式，不另造一套 runtime。
- 一個 Turn 從模型啟動到主動停止；一次 `ask() → message` 是一個 Round，工具在兩輪之間執行。
- 回合內追加指令仍屬同一 Turn；工具要求使用者輸入是 tool call 內部事件，不是新 Round。
- memory 用同一 resolver 接 `{"$ref":"memory:t7"}` 與 `[內容](memory:t7)`；link 不是授權。
- source trace 不因 compact 被改寫；每個 Round 保存實際送入模型的 context manifest，段落只從 working set 提升成 memory link。

## 尚未分類的新想法

目前沒有。新東西先加在這裡，不要直接塞進最像的 package；先問它是在做：

1. 傳輸資料；
2. 管理組織與工作；
3. 建立／限制 execution；
4. 管理 context／memory；
5. 觀察 effective state；
6. 還是修改 agentloop 本身。
