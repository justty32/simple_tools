# adapters

跨 process 或外部 host 的 control adapters。它們把 host 操作翻譯成 agentloop 公開 API，不擁有
模型 endpoint，也不修改 host 自己的 agent loop。

- [`pi/`](pi/README.md)：已實作的 Pi extension + Python JSONL bridge。

設計研究與其他 host 規劃見 [`docs/freepy/adapters/`](../../docs/freepy/adapters/README.md)。
