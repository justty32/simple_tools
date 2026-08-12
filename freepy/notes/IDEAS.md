# 想法收件匣

**只放還沒分類、還沒決定要不要做的東西。** 想到就先丟進來；一旦開始設計，就搬到對應的
`PLAN.md`。已經落地且需要解釋「為什麼」的決定才進 [NOTES.md](README.md)。

## 目前分類

跨 package 的依賴、里程碑與第一個實作切片集中在 [ROADMAP.md](../ROADMAP.md)；跨 package 的
思想綜合與 HTML 導覽見 [Agent World 設計報告](../../docs/design/agent-world/README.md)。

| 原想法 | 現在的位置 | 分類理由 |
|---|---|---|
| agent runtime 如同 OS；慢速機率 Lisp machine | [agent_machine/](../../docs/freepy/future/agent-machine/README.md) | 已整理成延後候選規格，不代表目前排程 |
| agent 之間寄信、查信、廣播 | [communication_tools/PLAN.md](../../docs/freepy/future/communication-tools/PLAN.md) | 這只是 transport，不是團隊 |
| 有上下級的多 agent 集合 | [team_tools/PLAN.md](../../docs/freepy/future/team-tools/PLAN.md) | 組織、授權、資源、任務與回報 |
| 產出 child agent、針對性 fork | [agent_runtime/PLAN.md](../../docs/freepy/future/agent-runtime/PLAN.md) | 涉及 lifecycle、sandbox、權限與預算守恆 |
| `$ref` 卸載／載入 | [memory_tools/PLAN.md](../../docs/freepy/future/memory-tools/PLAN.md) | 結構化記憶；同時支援 JSON `$ref` 與 Markdown link |
| context 段落提升成檔案、原位留連結 | [memory_tools/ORGANIZED-CONTEXT.md](../../docs/freepy/future/memory-tools/ORGANIZED-CONTEXT.md) | 不可變 trace、組織化 memory 與按步編譯的 working set |
| 我是誰、在哪、還剩多少 | [introspection_tools/PLAN.md](../../docs/freepy/future/introspection-tools/PLAN.md) | 唯讀觀察，不屬於 runtime mutation |
| 用檔案路徑存取 live agent state | [agentfs/PLAN.md](../../docs/freepy/future/agentfs/PLAN.md) | Plan 9／procfs 式的 synthetic filesystem projection |
| 用 Linux-as-Lisp 理解 agent namespace | [agentfs/LINUX-AS-LISP.md](../../docs/freepy/future/agentfs/LINUX-AS-LISP.md) | namespace 是環境、mount 是綁定、fd 是能力引用 |
| 回合、步、追加指令、工具輸入 | [agentloop/ROUNDS.md](../agentloop/ROUNDS.md) | agentloop 的活動與互動語意 |

## 已形成候選規格、尚未排程的決定

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

### 2026-08-11：Round 作為可在執行中改寫的函數

這則想法與後續的 assurance／人類介入／編輯器推論，已展開成
[Live Agent Machine 頭腦風暴報告](../../docs/explorations/live-agent-machine/README.md)。報告仍是研究，不表示
下列假說已升格為 Agent Machine 正式規格。

如果把一個 Step 看成 CPU 的一條指令，那麼一個 Round 就像一個 C 函數。然而 Round 並非
呼叫後便封閉不變：它在運作途中仍可追加指令、加入或替換 tool，並修改後續 Step 的執行
內容。

因此，交給 agent 的輸入可能不該被理解成普通的 prompt 或變數，而更像另一個仍在運作的
函數：它不是傳入後就放手的惰性資料，而會在被求值期間繼續與 Round 互動、改寫其環境與
後續行為。若採取這個模型，軟體的基本設計模式會產生質變；目前的假說是，只有 Lisp 式的
image、可持續求值與執行中重新定義，才足以自然表達它。

可以想像未來的編輯器：面前是一個等待指令、持續存在的 agent，如同一個 Lisp image；我們
編寫的不是一段送出即結束的 prompt，而是一個函數，交給這個 image 執行，並能在執行期間
繼續參與它的求值。

這也賦予先前的 **Linux as Lisp** 另一層意思。這並非只是假想中的未來介面：現在大家操作
coding agent 時，實際上已經把一個 workspace／資料夾交給 agent，告訴它「這就是你的
指令」。自然語言 prompt 只指出這次求值的入口；完整的函數體其實還包含資料夾中的程式、
文件、設定、目錄結構、工具入口與執行期間陸續發生的修改。

在這個基礎上繼續延伸，**編輯器就是 Linux 本身**。檔案系統是可見的程式表示，目錄是
可組合的 form／environment；我們新增、刪除、移動、連結或修改檔案與資料夾，就是在撰寫
和改寫 Lisp 函數。agent 對目錄的 walk／read／exec／write 是求值，mount 是建立 binding，
而執行中的 workspace 變更，則像在 Lisp image 裡重新定義函數後讓後續求值直接看到新版本。
VS Code、shell 或其他文字編輯器只是操作這個 image 的不同介面，並不是編輯器本體。

待釐清：這是在定義 Agent Machine 的求值模型，還是在擴充 agentloop 的 Round 控制語意；
以及「執行中的輸入函數」應以 continuation、actor、live object，還是 Lisp form／image 來
具體表示。另一個關鍵問題是：資料夾要如何成為 typed、可驗證的 form，而不退化成「任意
路徑都是命令」，也不讓其中的資料或記憶因為被載入就取得指令權限。

新東西先加在這裡，不要直接塞進最像的 package；先問它是在做：

1. 定義 Agent Machine 的求值、goal 或資源語意；
2. 傳輸資料；
3. 管理組織與工作；
4. 建立／限制 execution；
5. 管理 context／memory；
6. 觀察 effective state；
7. 還是修改 agentloop 本身。
