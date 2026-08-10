# 方法、快照與限制

## 調查方式

本次不是只讀 README。對每個 repository 都做 shallow clone，固定 HEAD，檢查：

- 公開定位、授權、版本與維護訊號；
- 核心資料型別、主迴圈、retry、handoff、state、persistence；
- 測試是否真的覆蓋宣稱；
- 對 freepy 現有程式與 `PLAN.md` 的增量價值；
- 安全、durability、並行與命名上的陷阱。

快照如下；連結固定到本次實際讀取的 commit：

| 專案 | 快照 | 授權 | 判讀 |
|---|---|---|---|
| [Swarm](https://github.com/openai/swarm/tree/6af0b4caf37dca4526dfd98e9fbd8ce36e7eeb22) | `6af0b4c` | MIT | 教學性、已被 Agents SDK 取代 |
| [Instructor](https://github.com/567-labs/instructor/tree/6754a32b1e35d57dfd94aea8099be68478f1e133) | `6754a32` | MIT | 活躍，正處 v1→v2 相容遷移 |
| [MiniChain](https://github.com/srush/MiniChain/tree/637d310ccd77dd7cb3197c826d0a304cafce65b2) | `637d310` | MIT | 2023 快照，無 repo tests |
| [PocketFlow](https://github.com/The-Pocket/PocketFlow/tree/f74d023f93607b8c3268133339a5e532a949898c) | `f74d023` | MIT | 100 行核心，大量 cookbook |
| [LightAgent](https://github.com/wanxingai/LightAgent/tree/2ea8917d75902c03d2a95af6fad2ba5aaa409ae2) | `2ea8917` | Apache-2.0 | v0.9.6，功能廣、快速演進 |
| [LangGraph](https://github.com/langchain-ai/langgraph/tree/d56666f7fbf0d380ad84cdf0cbe5aa48ab0cc086) | `d56666f` | MIT | 成熟的 stateful graph runtime |
| [CrewAI](https://github.com/crewAIInc/crewAI/tree/17f107c197e64ea486a1c985a36b3c4aecb20d28) | `17f107c` | MIT | Crews + Flows 的大型整合框架 |

## 規模不能直接比較

- Swarm 的 288 個 tracked files 中，多數是 examples/logs；真正 package 只有 6 個 Python files。
- Instructor 約 548 個 Python files、220 個 test-ish files，provider/mode matrix 本身就是主要複雜度。
- MiniChain 只有 19 個 Python files、約 1.4k Python 行，且沒有測試檔。
- PocketFlow 的 package 核心是單一 100 行檔案；其 537 files 大多是 cookbook。
- LightAgent 的核心約 7.2k Python 行，但 `core.py` 約 2.8k 行。
- LangGraph 是 monorepo，包含 graph、checkpoint、CLI、SDK 與多種 store；不能以 README quickstart 估成本。
- CrewAI 的 tracked files 超過 2.4 萬，約 2.27 萬在 `docs/`；核心規模應只看 `lib/`。

## 實跑與證據強度

- PocketFlow：以 repository 的 unittest suite 實跑，56/56 通過。
- Swarm：環境沒有 pytest，未安裝依賴污染工作環境；以主迴圈與 tests 靜態核對。
- 其他大型框架：未宣稱全套 tests 通過；以 source、tests、package metadata 交叉檢查。
- shallow clone 只保證當下 HEAD，不足以推論長期作者分布或 commit cadence。

所以報告中的「已實作」是有 source/test path 支撐；「成熟」「production-ready」等宣傳詞不直接當證據。

## 本地評估基準

對照的是目前工作樹中的：

- `freepy/agentloop/ROUNDS.md`：Step 是一次 ask→message；Round 是模型啟動到主動停止。
- `freepy/agent_runtime/PLAN.md`：spawn、policy derivation、instance lifecycle、sandbox。
- `freepy/team_tools/PLAN.md`：agent path、task、grant、resource ledger。
- `freepy/memory_tools/`：object/ref、organized context、Step manifest。
- `freepy/agentfs/PLAN.md`：只讀 synthetic filesystem projection。

未實作的 `PLAN.md` 不會被誤寫成現成功能；本報告只判斷哪些外部做法能縮短正確實作路徑。
