# Pi adapter

Pi coding agent extension 經 JSONL 啟動並控制一個獨立 Python agentloop Round。

- `pi-agentloop.ts`：Pi extension、slash commands 與 child lifecycle。
- `pi_bridge.py`：JSONL server，建立 bot／Handle／BackgroundRun。
- `check_pi_bridge.py`：完全離線的 subprocess protocol check。
- `examples/minimal_factory.py`：check 使用的 deterministic factory。

```sh
cd freepy
uv run python adapters/pi/check_pi_bridge.py
```

用法見 [`quickstart.md`](../../../docs/freepy/adapters/pi/quickstart.md)，完整設計邊界見
[`Pi adapter 文件`](../../../docs/freepy/adapters/pi/README.md)。
