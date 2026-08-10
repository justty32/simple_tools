# freepy

FreePy 是一組由小到大疊起來的 agent 元件。已完成的核心能對 OpenAI-compatible endpoint
發出模型請求、描述並執行工具，以及讓單一 bot 自主跑完一個 Round；多 agent、runtime、
memory 與 agentfs 仍在規劃階段。

## 目前可用

| 層 | 入口 | 角色 |
|---|---|---|
| 模型與工具契約 | [`llmkit/`](llmkit/README.md) | `llms`、LiteLLM proxy、`tooljson` |
| 模型 preset | [`llmkit/llms/presets.json`](llmkit/llms/presets.json) | id 對應 endpoint、model 與 parameters |
| 基本工具 | [`base_tools/`](base_tools/README.md) | 讀、寫、編輯檔案與 POSIX shell |
| 單 agent loop | [`agentloop/`](agentloop/README.md) | 阻塞 `run()`、limits 與可跨 thread 控制的 `Handle` |
| 使用介面 | [`shells/`](shells/README.md) | REPL 與外部 coding-agent shell |
| 原型 | [`prototypes/`](prototypes/README.md) | 尚未升格為穩定元件的可執行實驗 |

[`try.py`](try.py) 是串流、思考與工具呼叫的 worked example，不是穩定 API。

## 規劃中的架構

跨 package 的實作順序以 [`ROADMAP.md`](ROADMAP.md) 為準：

```text
agent_machine：event、resource、scheduler、goal 的 userspace domain kernel
      ├─ agentloop / llmkit：Round runner 與 endpoint
      ├─ agent_runtime：Linux worker 與 sandbox
      ├─ memory_tools：context pager
      ├─ team / communication：組織與 IPC
      └─ introspection / agentfs：effective state projection
```

Agent Machine 的獨立規格在 [`agent_machine/`](agent_machine/README.md)；各元件細節在自己的
`PLAN.md`。[`IDEAS.md`](IDEAS.md) 只收尚未分類的新想法，跨元件的完整設計底圖在
[`../docs/agent-world/`](../docs/agent-world/README.md)。

## 文件角色

- [`NOTES.md`](NOTES.md)：已落地決策與實測原因；不是使用說明。
- [`NOTES-llmkit.md`](NOTES-llmkit.md)：llmkit 定型前的已結案紀錄。
- [`ENV.md`](ENV.md)：本機開發環境的特殊設定。
- `PLAN.md`：尚未完成的規格；若與較新的定案文件衝突，以最後編輯者為準。
- `README.md` 與程式 docstring：目前行為與使用契約。

時間語彙以 [`agentloop/ROUNDS.md`](agentloop/ROUNDS.md) 為唯一來源：完整 `run()` 是 Round，
每次 `ask() → message` 是 Step，工具在兩個 Step 之間執行。

## 離線驗證

```sh
cd freepy
PYTHONPATH=llmkit uv run python -m base_tools
PYTHONPATH=llmkit uv run python -m agentloop
cd llmkit
uv run python -m tooljson
```

需要模型／proxy 的 smoke test 另見各 package README。
