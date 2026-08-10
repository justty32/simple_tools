# 評估方法與判準

## 不只問「能不能寫」

幾乎所有一般程式都能用 Lisp 寫；有用的問題其實有三層：

1. **語意可表達嗎？** 資料模型、規則與狀態轉移是否適合 Janet。
2. **整合有證據嗎？** HTTP、JSON、process、async、FFI 在目標環境是否真的跑過。
3. **值得成為 production owner 嗎？** 維護、除錯、安全邊界和生態成本是否比 Python 更好。

本報告的等級：

| 等級 | 含義 | 遷移策略 |
|---|---|---|
| A | 純資料／純規則，高度適合 | 優先 Janet 固化 |
| B | 核心適合，但需要 effect adapter | Janet core + 外部 adapter |
| C | 能寫，現有證據或生態不足 | 保留 Python／OS backend，之後再評估 |
| D | Python 特有或只是探索物 | 不移植，只保留相容邊界 |

## 盤點邊界

「已實作」只計目前有 Python 程式與離線 smoke 的部分：

- `llmkit/llms`、`llmkit/tooljson`
- `agentloop`、`base_tools`、`llms` presets
- `shells`、`exec_tools/discover.py`
- `prototypes/plan_shell.py`、`try.py`

「已規劃」則是尚無對應實作或只有部分骨架：

- `agent_runtime`、`communication_tools`、`team_tools`
- `memory_tools`、`introspection_tools`、`agentfs`
- Round 內追加指令與 tool-input channel
- `exec_tools` 的 describe／invoke 等後續層

不能把完整的 `PLAN.md` 當作已交付能力。

## 固化的合格條件

某功能進入 Janet 前，至少滿足：

- 外部可觀察行為能用 JSON、bytes、argv、event trace 描述。
- schema 是 source of truth，不依賴 Python object identity 或 reflection 才成立。
- 有正常、邊界、錯誤與重播 fixtures。
- pure decision 與 side effect 已分離。
- Python reference 與 Janet 對同一 fixture 給出等價結果。
- 失敗模式明確；不能只比 happy path。

若規格仍頻繁改、provider 行為還在探索，繼續用 Python比較便宜。固化不是「demo 一跑就重寫」，而是「語意穩定後把它變成第二個可驗證實作」。

## 證據強度

由強到弱：

1. 本機離線測試與 wire test。
2. repo 中已運作的 Janet 模組。
3. 文件記錄的實測限制。
4. 語言 API 存在。
5. 僅憑 Lisp／Unix 類比的推論。

報告刻意保留反證：Windows subprocess crash、HTTP 無 TLS/SSE、FUSE/9P 無實作，都會降低評級。

## 主要來源

- [freepy ROADMAP](../../freepy/ROADMAP.md)
- [freepy IDEAS](../../freepy/IDEAS.md)
- [freepy 工作筆記](../../freepy/NOTES.md)
- 外部工作區的 `langlab-janet/FINDINGS.md`
- 外部工作區的 `langlab-janet/MISSING.md`

`langlab-janet` 不在本 repository 內；以上是 2026-08-10 評估時讀取的相鄰工作區材料，
不是本文件樹內可攜的連結。
