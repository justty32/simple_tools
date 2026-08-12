# http_tools

`http_tools` 是 `tooljson` 的 typed network effect：可信 spec 固定 endpoint 與 method，模型只填
query／JSON 參數。

```python
import http_tools

schemas, dispatch = http_tools.tools("weather.json")
result = dispatch["lookup_weather"](city="Taipei", days=3)
```

import package 時會以 `tooljson.register("http", HttpBody)` 登記 `_type: "http"`；因此先
`import http_tools` 後，也能直接用 `tooljson.load()` 或讓 `exec_tools` 掃描混合 spec catalog。
完整 wire contract 見 [`HTTP.md`](HTTP.md)。

這不是任意網頁抓取器，也不是 sandbox。URL 不由模型提供、redirect 不跟隨、request/response
有大小與時間上限，也可用 `set_approver(fn)` 在送出前人工批准；但真正的 DNS、route、proxy、
credential 與 egress policy 仍屬 runtime／OS。

## 驗證

```sh
cd freepy
PYTHONPATH=llmkit uv run python -m http_tools
```

測試只啟動 `127.0.0.1` 暫存 HTTP server，不連外網。
