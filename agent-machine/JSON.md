# AgentOS JSON 共通規則

本頁只處理 Python、C++、Janet 最容易做出不同結果的地方。使用者仍讀寫普通 JSON。

## 讀取

- 檔案是 UTF-8；可有一般 whitespace，不能有 BOM。
- object 不能有重複 key；string 不能有 unpaired surrogate；違反就整份拒絕。
- 只收 JSON 的 null、boolean、string、array、object 與 finite number；不收 NaN／Infinity。
- 所有 JSON number 先成為 finite IEEE-754 binary64。schema／structural integer 不看 token 寫法；只要
  `trunc(x)==x` 且在 `-9007199254740991..9007199254740991` 就接受。因此 `1`、`1.0`、`1e0` 都是 integer 1，
  `1e20` 不是可接受 integer。更大的精確值請用 string。
- string 不做 Unicode normalization，讀到什麼 code points 就保留什麼。
- domain schema 的未知 key 預設拒絕；只有文件明說可擴充的 object 才可保留未知 key。

## canonical bytes 與 hash

需要 hash 時直接使用 [RFC 8785 JSON Canonicalization Scheme](https://www.rfc-editor.org/rfc/rfc8785)：object key
依 RFC 的 UTF-16 code-unit 規則排序、array 保持順序、number 使用 ECMAScript/JCS 表示、不輸出額外 whitespace，
最後編成 UTF-8。不要用 Python `sort_keys` 或某個 C++ library 的 default dump 冒充。

hash 一律是 canonical bytes 的 SHA-256，以 64 個小寫 hex 表示。用途固定：

- model lock key：`[engine, endpoint, model]`。
- tool spec hash：`{"origin":ABSOLUTE_LEXICAL_ORIGIN, "spec":TOOL_OBJECT}`；origin 不同，tool 就不同。
- instruction 的 public input hash：已解析但已遮蔽 secret 的 immutable input。

真正 secret 不進 log/hash；`$env` 使用 raw node，例如 `{"$env":"LLM_API_KEY"}`，inline secret 則換成固定
`"<redacted>"`。所以 hash 只抓公開設定／input，不證明 secret value 相同；runtime 仍只在 memory 用真值。

## v1 tool schema 子集

v1 不承諾完整 JSON Schema，只接受執行 tools 真正需要的小集合。tool 外殼恰好是 `type/function/_extra`；
`type` 必須是 `function`。`function` 只接受：

```json
{
  "name":"read_file",
  "description":"Read one text file.",
  "parameters":{
    "type":"object",
    "properties":{"path":{"type":"string"}},
    "required":["path"],
    "additionalProperties":false
  }
}
```

- `name` 是 1..64 個 ASCII letter、digit、`_` 或 `-`；同一 Bot 不可重複。
- `description` optional string；`parameters` required。
- parameters root 只接受 `type/properties/required/additionalProperties`。type 必須是 object，properties required，
  required default `[]` 且不可重複，additionalProperties 必須明寫 false。
- property schema 只接受 `type/description/enum/minLength/maxLength/minimum/maximum/items`。
- type 只可為 string、integer、number、boolean，或 items 為上述 scalar 的 array。v1 不收 nested object。
- min/max 只可用在相符型別；string 長度計 Unicode scalar values。enum items 必須同型別且不重複。
- 驗證不做型別轉換、不套 default、不把 format 當驗證規則。
- v1 拒絕 schema `$ref`、anyOf/oneOf/allOf 與其他 keyword，也不讀 file/network schema。

`_extra`、argument validation 與固定錯誤格式見 [`TOOL_SPEC.md`](TOOL_SPEC.md)。

這個小子集會由 shared JSON fixtures 固定：同一份 valid/invalid corpus 必須在 Python、C++、Janet 得到相同
結果。未來確實需要 nested arguments 或完整 JSON Schema 時升新 format version，不靜默放寬 v1。
