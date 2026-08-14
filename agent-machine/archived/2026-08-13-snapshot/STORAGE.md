# AgentOS Bot 成員儲存格式

public `llm/messages/tools` 顯示 effective JSON；private raw 保存在 `.agentos/`。三個 member 各自知道哪些
欄位是 composition directive，不能拿 generic resolver 對所有 JSON 盲目展開。

```text
bot-a/.agentos/
  llm.json
  messages.json
  tools.json
```

第一層相對 path 都以 Bot root 為 base，與 private 檔實際位於 `.agentos/` 無關。

三份 root manifest 都拒絕未知 key，`format` 必填且必須完全等於下列 v1 字串：

- LLM：required `format, source, local`。source 是 inline object／`$ref`，local 是 object。
- messages：required `format, source, history`；optional `system` 是 string 或 `$env` node。source/history 是 array。
- tools：required `format, source`，source 是 array。

未知 format 直接拒絕，不把新版本當舊版本猜。`agentos new` 會把空 array/object 明寫出來。所有 JSON 的共同
讀取、canonical hash 與 v1 tool-schema 子集見 [`JSON.md`](JSON.md)。

## LLM raw

```json
{
  "format":"agentos.llm.raw.v1",
  "source":{"$ref":"../shared/ollama.json"},
  "local":{
    "parameters":{"temperature":0.1}
  }
}
```

```text
effective = deep_merge(resolve(source), resolve(local))
```

- source 可以是 inline object 或 `$ref`，解析後必須是 LLM object。
- local 必須是 object；object 遞迴合併，array/scalar 由 local 完整取代。
- `llm set temperature 0.1` 只寫 `local.parameters.temperature`。
- `unset` 只刪 local leaf，再清掉空 parent；source 值會重新露出。
- `llm use FILE` 只替換 source、保留 local override。
- model switch 先用 candidate effective config 完成安全 unload，再 atomic replace raw；失敗時 raw 不變。

永遠不把 effective object 寫回 raw，所以 shared ref 不會被 flatten 或偷偷改寫。

## Messages raw

```json
{
  "format":"agentos.messages.raw.v1",
  "source":[
    {"$ref":"../shared/system-message.json"},
    {"$ref":"../shared/context-message.json"}
  ],
  "system":"Reply briefly.",
  "history":[]
}
```

- source item 可以是 inline message 或 `$ref`；每一項解析後必須恰好是一則 canonical message，不接受 array。
- source 最多一則 system，而且只能是第一則。
- optional local system string 取代 source system；key 不存在就繼承 source system。
- history 只放 runtime 追加的 user／assistant／tool messages。
- effective 順序是 system override → source 非 system messages → history，最後做完整 pairing validation。
- history 是 opaque runtime data，絕對不跑 `$ref`／`$env` resolver。tool arguments 中的 `{"$ref":...}`
  很可能只是模型真的給出的資料。

公開操作：

- `messages use FILE...` 替換 source list，保留 local system/history；只在沒有 active Run 時做。
- `set system` 寫 local string；`unset system` 刪 key，重新露出 source system。
- `clear` 清 history，也從 source list 移除非 system messages；保留 raw system ref 與 local system。
- `clear --all` 清 source、local system 與 history，effective 變成空 array。
- runtime message 永遠只 append history，不回寫 source。

若要共用一段多 message context，v1 請逐項用 fragment：

```json
{"$ref":"../shared/context.json#messages/0"}
```

這個限制讓 clear、pairing 與來源追蹤都保持明確；需要 array include 時再另訂新版本，不暗中做 array merge。

## Tools raw

```json
{
  "format":"agentos.tools.raw.v1",
  "source":[
    {"$ref":"../shared/read-tool.json"},
    {
      "type":"function",
      "function":{
        "name":"ask_user",
        "description":"Ask the user one question.",
        "parameters":{
          "type":"object",
          "properties":{"question":{"type":"string", "minLength":1}},
          "required":["question"],
          "additionalProperties":false
        }
      },
      "_extra":{"_version":"0.1.0", "_type":"builtin", "name":"ask_user"}
    }
  ]
}
```

- source item 可以是 inline spec 或 `$ref`；每一項解析後必須恰好是一個 tool object，不接受 array。
- `tools add FILE[#fragment]` 驗證後只 append raw ref。
- `tools remove NAME` resolve source，找到唯一名稱後刪除 Bot raw list 裡的那一項；外部檔完全不改。
- duplicate name、找不到 name、未知 executor type、pending call 所需 tool 都直接拒絕。
- 一個檔有多個 tools 時，逐項加入 fragment，例如 `bundle.json#tools/0`。
- builtin 的公開 function name、private executor name 與固定 schema 必須完全相符；`ask_user` 只接受一個
  非空的 `question` string。少給、多給或型別錯誤都形成 `failed` tool result，不進 waiting。

tool spec 解析出來後視為 opaque。標準 JSON Schema 自己也使用 `$ref`，不能被 AgentOS 當成 include；tool
schema v1 直接拒絕 `$ref`，不做第二套 file/network resolver。tool credential 也不放 spec，日後交給明確
secret broker。

composer 內部不可只回傳 JSON；每個 effective tool 是 `ResolvedTool {spec, origin}`。inline spec 的 origin
是 Bot root；每走一層 `$ref` 就改成該來源 JSON 的 lexical directory，fragment 不改 origin。公開 `./tools`
只輸出 spec，origin 是 private metadata。tool hash 對 `{origin,spec}` 計算，所以相同 JSON 從不同目錄載入
會指向不同 executable。

## `$ref`／`$env` 的作用位置

只有 member composer 指定的位置會解析：

| member | 解析位置 | opaque data |
|---|---|---|
| LLM | source/local 內的 config | adapter 明確標成 raw payload 的欄位 |
| messages | source item、local system `$env` | resolved message、history、call arguments |
| tools | source item | 整個 resolved tool spec；schema `$ref` 在 v1 拒絕 |

reserved node 的 keys 必須恰好是 `{"$ref"}`、`{"$env"}` 或 `{"$env","default"}`。同時含兩者、帶
sibling 或出現在不允許的 composition 位置都 fail closed，不默默忽略。

## Atomic mutation 與跨語言檢查

三份 raw manifest 都在 state lock 下以 temp → fsync → atomic rename → fsync parent 更新。source file hash
在 set/unset/clear/add/remove 前後必須不變。Python、C++、Janet 用同一批 golden fixtures 驗證：

- raw 保留 refs，effective 頂層嚴格為 object／array／array。
- private physical path 不改變 Bot-root relative ref。
- history arguments 的 `$ref` 不被解析；schema `$ref` 不被當 include，之後由 v1 validator 拒絕。
- LLM set/unset、messages system/clear、tools add/remove 結果完全相同。
