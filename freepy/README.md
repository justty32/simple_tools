# FreePy

FreePy 是一組由小到大疊起來的 agent 元件。這個目錄現在以「可執行程式」為主；跨 package
架構、研究與尚未實作的規格都集中在 [`docs/freepy/`](../docs/freepy/README.md)。

## 目前可用

| 元件 | 角色 |
|---|---|
| [`llmkit/`](llmkit/README.md) | `llms` endpoint client、LiteLLM proxy、`tooljson` 格式與 dispatch |
| [`base_tools/`](base_tools/README.md) | 讀寫、編輯檔案與 POSIX shell 的基礎工具 |
| [`agentloop/`](agentloop/README.md) | 單一 Round 的 `run()`、`advance()`、Handle、Limits 與 Controller |
| [`shells/`](shells/README.md) | Python REPL 與 coding-agent 的薄 launcher |

`Handle` 是刻意直接、可變的本地把手；`Controller` 是組合 Handle 與 runner 樣板的方便
wrapper，不是持久化 control plane。完整分層見
[`docs/freepy/architecture.md`](../docs/freepy/architecture.md)。

## 其他程式區

| 目錄 | 性質 |
|---|---|
| [`adapters/`](adapters/README.md) | 跨 process／host 的 control adapters；目前有 Pi bridge |
| [`examples/`](examples/README.md) | 可執行的使用範例，不是穩定 API |
| [`prototypes/`](prototypes/README.md) | 尚未升格、可能被推翻的實驗 |
| [`notes/`](notes/README.md) | 已落地決策、實測與想法收件匣 |

## 規劃與研究

- [`ROADMAP.md`](ROADMAP.md)：目前實作順序；先本地 core／Controller／interfaces，再看是否需要
  durable service。
- [`docs/freepy/future/`](../docs/freepy/future/README.md)：Agent Machine、runtime、memory、team 等
  延後規格。
- [`docs/guides/roadmap/`](../docs/guides/roadmap/index.html)：白話 HTML 導讀。
- [`docs/design/agent-world/`](../docs/design/agent-world/README.md)：跨元件設計底圖。

## 離線驗證

```sh
cd freepy
PYTHONPATH=llmkit uv run python -m base_tools
PYTHONPATH=llmkit uv run python -m agentloop
uv run python adapters/pi/check_pi_bridge.py
cd llmkit && uv run python -m tooljson
```

這組命令以 Linux／WSL 為準：`base_tools.run_shell` 刻意只支援 POSIX，`tooljson` 的 exec
fixtures 也是可執行的 POSIX scripts。Windows 原生可直接跑 agentloop 與 Pi bridge；前後兩項請進
WSL，不能把 Win32 的預期拒絕誤判成搬家回歸。

需要真模型或 proxy 的 smoke test 另見各 package README。
