# AgentOS 設定解析規則

LLM、messages、tools 共用 `$ref`／`$env` 語法，但由各 member composer 決定哪些欄位可解析。這份規則
是 Python、C++ 與 Janet/Lisp 必須產生相同結果的資料契約。

## 三種查看方式

```sh
./llm             # effective view
./llm raw         # Bot 自己保存的原文
./llm check       # 解析並檢查，但不執行、不修改
```

`messages` 與 `tools` 相同。`raw` 顯示 private manifest 原文；effective view 只在
[`STORAGE.md`](STORAGE.md) 指定的 composition positions 展開，再用該成員 schema 檢查。解析或檢查
失敗時回 non-zero，正式設定保持原樣。

## `$ref`

最小形式：

```json
{"$ref":"../shared/ollama.json"}
```

- Bot 成員的 root document 以 Bot 根目錄為 logical base，不受 private 實體檔放在哪裡影響。
- 進入被引用的 JSON 後，其中下一層相對 `$ref` 才以該 JSON 檔所在目錄為準。兩種情況都不受 shell
  cwd 影響。
- Bot root 先由薄 wrapper 的 `pwd -P` 固定。之後 path 只做 lexical join/normalize（Python `normpath`、C++
  `lexically_normal`），不呼叫 `realpath`／`weakly_canonical`。OS 可以照常跟 symlink，但 logical origin 不跳到
  target 目錄；link target 改變會在下一次 compose 看見，v1 不提供 symlink identity 或追蹤保證。
- `file.json#part/name` 依 `/` 逐層找 object key；陣列使用 `#items/0`。v1 不提供跳脫字元，因此含
  `/` 的 key 不能用 fragment 指到。
- `$ref` node 必須只有 `$ref` 一個 key。多餘 sibling 一律由 `check` 拒絕，不默默忽略。
- ref 不存在、fragment 不存在、JSON 無效或形成 loop，都使整次解析失敗。
- composer 還在讀 composition data 時可以繼續解析下一層；一旦得到 canonical message、runtime history 或
  tool spec，就視為 opaque。tool schema 內的 `$ref` 不是 include，而且 v1 validation 直接拒絕。

多個 object 可以由前到後套用：

```json
{"$ref":["base.json", "local.json"]}
```

規則只有一種：

- object 與 object：依 key 遞迴合併，後面的值優先。
- 其他任何組合：後面的值完整取代前面的值。
- array 不做自動 append、去重或依名稱合併。

多 object merge 只用於需要 object 的位置，例如 LLM source。messages／tools 的 raw manifest 則逐項列
source，而且 v1 每個 source 只產生一項：

```json
{"source":[
  {"$ref":"messages/system.json"},
  {"$ref":"messages/context.json"}
]}
```

## `$env`

```json
{"$env":"OLLAMA_API_KEY"}
```

或提供非 secret 的預設值：

```json
{"$env":"BOT_LANGUAGE", "default":"zh-TW"}
```

- `$env` 必須是 env 名稱字串；node 只准再有 optional `default`。
- env 不存在或是空字串時使用 `default`；沒有 default 時得到 JSON `null`，再交給 schema 判斷能否接受。
- 讀到的 env value 永遠是字串，不偷偷猜 number、boolean 或 JSON。`default` 則保留原本 JSON type。
- 選中的 default 是 terminal literal，不再把其中長得像 `$ref`／`$env` 的 object 繼續解析。
- secret 不應寫在 `default`。effective view 依 schema 遮蔽 secret，但 runtime 內部執行時使用真實值。
- raw view 只顯示 env 名稱，不把目前 process 的 secret 寫回檔案。
- v1 只在 LLM config 與 messages local system 等 composer 明確允許的位置解析 `$env`。runtime history、
  tool arguments、tool spec／JSON Schema 都不解析。

## set、unset 與路徑參數

LLM 的 `set` 修改本 Bot raw manifest 的 local override，不修改 `$ref` 指向的檔案：

```sh
./llm set temperature 0.1
./llm unset temperature
```

`unset` 只移除 local override；若 source 提供同名值，effective view 會重新露出 source 值。命令列 value 先依
目標 schema 解碼，所以 `0.1`、`4096`、`true` 是 JSON scalar，model 名稱仍是 string。

`agentos new bot-a` 的 path 依普通 shell cwd 解讀；Bot 已建立後，成員命令收到的相對 path 一律以 Bot
根目錄解讀：

```sh
./tools add ../shared/read-tool.json
```

因此 `cd bot-a; ./tools ...` 與從外面呼叫 `./bot-a/tools ...` 的意思相同，符合「Bot 是物件」。AgentOS
驗證後，保存成相對於 Bot root logical base 的 `$ref`，讓 Bot 目錄搬家時盡量仍可用。未特別指定 workspace
的 tool 也以 Bot 根目錄為 cwd。v1 不追蹤 symlink target；symlink 行為留待之後獨立支援。

## 每一步留下實際設定

每條 LLM／tool instruction 開始前，從三個 raw manifests compose 一次 effective snapshot，並在 event 保存
格式版本與 [JCS canonical hash](JSON.md)。history 與 tool schema 的 opaque JSON 不再走 resolver。
raw secret、展開後的 secret 與完整敏感 messages 預設不寫入 log。如此日後可知道某一步用了哪份設定，
又不把 API key 當成除錯資料散出去。
