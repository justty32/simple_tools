# freepy — 工作筆記

這裡保存跨 session 的決定、實測結果，以及不看 code 看不出的理由。公開用法在
[`llmkit/`](../llmkit/README.md) 與各 package README；尚未決定的想法放
[`IDEAS.md`](IDEAS.md)，本機設定放 [`ENV.md`](../../docs/development/freepy-environment.md)。

詳細筆記按主題拆開：

- [`NOTES-agentloop.md`](agentloop.md)：agentloop 的順序、限制、quiet、離線驗證，以及
  多 agent package 分層。
- [`NOTES-tooling.md`](tooling.md)：base_tools／tooljson 的邊界、四層路線與 llmkit 成形。
- [`NOTES-modelcards.md`](archive/modelcards.md)：已退場 modelcards 的設計背景與實測反證。
- [`NOTES-llmkit.md`](llmkit.md)：llmkit 定型前的 Reply、stream、caps 與 endpoint 經驗。
- [`Python 互動 API`](python-interaction-api.md)：Bot／LLM 分工、Round ownership、換模型慣例與
  公司 Ollama live 驗收。

## 2026-08-10 modelcards 退場：preset 只保留執行所需資料

通勤檢視後確認原本的 modelcards 過度設計。`claimed`／`verified`、sources、weights、
runner、alias、mode、table 與專用 research workflow 全部退出 runtime；舊檔可由 Git 歷史
追溯，不另建 archive 副本。

現在唯一資料是 [`llmkit/llms/presets.json`](../llmkit/llms/presets.json)：以 id 為 key，
每筆只含 `endpoint`、`model`、`parameters` 和可省略的 `description`。`llms.load_preset(id)`
直接建立 `LLM`。能力仍由 proxy／`LLM.caps` 處理，不在 preset 維護第二份。

2026-08-09 的 modelcards 設計與實打結果移到 [`NOTES-modelcards.md`](archive/modelcards.md)，
只作被本決定取代的歷史背景，不描述目前介面。

## 慣例

- Markdown 單檔低於 8 KiB；程式約 150 行內。超過就依責任拆檔，由短 README／索引導覽。
- 沒有獨立 test suite，驗證靠可執行關卡：
  - `cd freepy/llmkit && uv run python -m tooljson`
  - `PYTHONPATH=llmkit uv run python -m base_tools`
  - `PYTHONPATH=llmkit uv run python -m agentloop`
  - `uv run python -m llms <一般模型> <思考模型>`（需要 proxy）
- 串流＋工具、串流＋思考等未被 `__main__.py` 涵蓋的組合要手動實測；
  [`examples/llm_tool_roundtrip.py`](../examples/llm_tool_roundtrip.py) 是串流＋工具範例。
- 不連 proxy 也能驗 `Reply`：用假的 response 餵 `Reply(...)`，串流與非串流應得到相同的
  `text`、`calls` 和 `history`。
