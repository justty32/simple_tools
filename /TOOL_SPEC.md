# AgentOS v1 tool spec

本頁固定 tool JSON 的 strict shape。模型只看到 `type/function`；AgentOS 另外讀 `_extra` 執行它。

## 公開外殼

root 必須恰好有 `type, function, _extra`，`type` 必須是 `"function"`。function 與 parameters 的完整小型
schema 見 [`JSON.md`](JSON.md)；unknown key、完整 JSON Schema 與 schema `$ref` 都拒絕。

## exec `_extra`

| key | required/default | type / range |
|---|---|---|
| `_version` | required | 恰好 `"0.1.0"` |
| `_type` | required | 恰好 `"exec"` |
| `exec` | required | 1..64 個 non-empty string |
| `argv` | `{}` | object，key 必須是 parameters property |
| `stdin` | `null` | null 或恰好 `{"param": NAME}` |
| `cwd` | `null` | null 或 non-empty string，最多 4096 UTF-8 bytes |
| `timeout` | 60 | finite number，0.001..86400 秒 |
| `stdout` | `{}` | 下表 object |
| `stderr` | `{}` | 下表 object |
| `ok_exit` | `[0]` | unique integer array，每項 0..255 |
| `limits` | `{}` | property name 到 limit object |

所有 object 都拒絕未列 key。string path/argv 含 U+0000 拒絕。argv mapping：

| key | default | type / range |
|---|---|---|
| `position` | 0 | integer 0..65535 |
| `flag` | absent | non-empty string，最多 1024 UTF-8 bytes |
| `separate` | true | boolean；false 時必須有 flag |
| `repeat` | false | boolean；只可綁 array property |

每個 parameters property 必須恰好由 argv 或 stdin 使用。stdin param 必須是 string property；不能同時在
argv。argv 未寫 `repeat:true` 不可綁 array。boolean flag、排序、number 轉字串等見 [`EXEC.md`](EXEC.md)。

stdout 只接受 `clip`（`head|tail`，default head）與 `max_bytes`（integer 64..1048576，default 65536）。
stderr 只接受 `mode`（`merge|ignore|only`，default merge）。

limits 的每個 value 是 non-empty object，只接受：

- `max_bytes`：integer 1..16777216，只可用於 string。
- `min`／`max`：finite number，只可用於 integer/number；兩者都有時 min <= max。

## builtin `_extra`

v1 唯一 builtin 是 `ask_user`。它的 `_extra` 必須恰好是：

```json
{"_version":"0.1.0", "_type":"builtin", "name":"ask_user"}
```

public function name 必須也是 `ask_user`；parameters 必須等價於 [`STORAGE.md`](STORAGE.md) 的固定 schema，
不能加參數或放寬 additionalProperties。未來 builtin 以新 name 加入，每個各自凍結 schema。

## arguments 驗證與唯一錯誤

不做 coercion/default。一次只回第一個 error，排序固定：

1. arguments 不是 object／raw JSON 壞掉。
2. unknown key，依 JCS property order 第一個。
3. missing required，依 JCS property order 第一個。
4. known properties 依 JCS order；每個依序檢查 type、enum、minLength、maxLength、minimum、maximum。
5. array items 依 index 小到大，用相同 property 檢查順序。
6. exec limits 在 schema 之後，依 property JCS order檢查 max_bytes、min、max。

結果固定為：

```text
Error [invalid_arguments]: JCS_DETAIL
```

detail 只能是以下形狀；`argument` 是 property name，root 用 `$`，array item另加 `index`：

```json
{"argument":"$", "reason":"invalid_json"}
{"argument":"$", "reason":"wrong_type", "expected":"object"}
{"argument":"path", "reason":"unknown"}
{"argument":"path", "reason":"missing"}
{"argument":"width", "reason":"wrong_type", "expected":"integer"}
{"argument":"mode", "reason":"not_allowed"}
{"argument":"name", "reason":"too_short", "limit":1}
{"argument":"text", "reason":"too_large", "limit":4096, "unit":"bytes"}
{"argument":"width", "reason":"out_of_range", "min":1, "max":8192}
{"argument":"tags", "index":2, "reason":"wrong_type", "expected":"string"}
```

只放有意義的 `min/max/limit/unit/index`。schema maxLength 的 unit 是 `characters`，exec max_bytes 是 `bytes`；
minimum／maximum 共用 out_of_range。enum 使用 not_allowed。`ask_user` 也走這套 validator，不能另用 Python
exception 文字。完整模板會成為跨語言 golden fixtures。
