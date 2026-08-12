# `_type: "http"`

`http_tools` 在 `tooljson` 外殼上登記一種固定 endpoint 的 HTTP effect。模型不能提供 URL，只能填
schema 已宣告且明確映射到 query／JSON body 的參數。

```json
{
  "type": "function",
  "function": {
    "name": "lookup_weather",
    "description": "查一個城市的公開天氣資料",
    "parameters": {
      "type": "object",
      "properties": {
        "city": {"type": "string"},
        "days": {"type": "integer"}
      },
      "required": ["city"]
    }
  },
  "_extra": {
    "_version": "0.1.0",
    "_type": "http",
    "method": "POST",
    "url": "https://weather.example/api/lookup",
    "query": {"city": "q"},
    "json": {"days": "forecast_days"},
    "headers": {"Accept-Language": "zh-TW"},
    "timeout": 30,
    "ok_status": [200],
    "max_response_bytes": 30000,
    "limits": {"city": {"max_bytes": 200}, "days": {"min": 1, "max": 14}}
  }
}
```

## 規則

- `url` 必須是完整 `http`／`https` URL，不准含帳密、query 或 fragment。
- `method` 支援 `GET`、`HEAD`、`POST`、`PUT`、`PATCH`、`DELETE`，預設 `GET`。
- `query`／`json` 都是 `{模型參數名: wire 名稱}`；每個 schema property 必須恰好映射一次。
- GET／HEAD 不可有 JSON body。JSON 固定以 UTF-8 compact encoding 送出。
- query 編碼後上限 8192 bytes，JSON body 上限 65536 bytes。
- `headers` 是可信 spec 的靜態值。禁止 `Host`、`Content-Length`、`Transfer-Encoding` 與
  `Accept-Encoding`；參數不能映射成 header。
- `timeout` 範圍是大於 0、最多 300 秒；response raw bytes 預設最多讀 30000、最大可設 4 MiB。
- 預設 2xx 都成功；其他 status 回 `HTTP N` 與有界 body。`ok_status` 可明確改寫。
- redirect 不跟隨，避免固定 endpoint 在執行期悄悄換成另一個目的地。
- 執行錯誤永遠回字串；spec 格式錯誤則在 `load()` 時丟 `SpecError`。

預設允許執行。需要逐次批准時：

```python
http_tools.set_approver(
    lambda name, method, url, args: input(f"{method} {url} ? [y/N] ") == "y"
)
http_tools.set_approver(None)
```

hook 只屬於 HTTP type；`tooljson.set_approver()` 仍只管 exec，兩種 effect 不假裝共用一種 policy。

## 安全邊界

固定 URL 比讓模型傳任意 URL 少一層 SSRF 入口，但它不是 network sandbox：DNS、proxy、路由和
宿主 process 的網路權限仍由作業系統決定。spec 本身是可信設定；不要把 API key、cookie 或
bearer token 寫進 JSON。需要 credential 時應由未來 secret broker 在受控 route 上注入。

這版刻意不做 URL template、redirect、multipart、streaming、下載檔案、任意 headers 或模型控制的
HTTP method。這些需求出現時應增加明確 mapping／broker，而不是開一個 `fetch(url, ...)` 萬用洞。
