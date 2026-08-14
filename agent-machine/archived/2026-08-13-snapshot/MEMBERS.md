# AgentOS Bot 三個成員

Bot 對外只有三份核心資料：LLM、messages、tools。`start/status/run` 是方法，不是第四份 Bot 資料。

## LLM

最小 LLM source object：

```json
{
  "engine":"openai",
  "endpoint":"http://localhost:4000/v1",
  "model":"qwen3.5:9b",
  "api_key":{"$env":"LLM_API_KEY"},
  "timeout":300,
  "parameters":{
    "temperature":0.1,
    "max_tokens":4096
  }
}
```

- `engine` 選擇 executor adapter；Python v1 先有 `echo`、OpenAI-compatible 與 direct Ollama。
- `endpoint`、`model` 是 string。echo 不需要它們；其他 engine 依自己的 schema 檢查。
- `api_key` 可省略，通常用 `$env`；effective view 會遮蔽。
- `timeout` 是整次 request 秒數，不是 Run 總時間。
- `parameters` 是送給模型的生成參數。canonical names 至少含 temperature、top_p、max_tokens、seed、
  stop、presence_penalty、frequency_penalty；engine-specific key 必須由該 adapter 明確接受。
- live LLM 沒有另設時，`max_tokens` effective default 是 4096；每次 instruction event 記下實際值。

effective object 只接受 `engine/endpoint/model/api_key/timeout/parameters`：engine required；其餘依 adapter
required 或 optional，parameters default `{}`、timeout default 300。未知 top-level key 與 adapter 不認得的
parameter 都拒絕；共同 JSON 規則見 [`JSON.md`](JSON.md)。

上例 `$env` 只存在 raw source/hash projection；runtime effective 是真實 secret string，公開 `./llm` view
固定顯示 `"<redacted>"`。secret 不回寫 manifest 或 log。

日常命令不要求使用者寫 `parameters.`：

```sh
./llm set model qwen3.5:9b
./llm set temperature 0.1
./llm set max_tokens 4096
./llm unset temperature
./llm use ../shared/ollama.json
./llm set key-env LLM_API_KEY
./llm check
./llm unload
```

`set key-env` 保存 `$env` node，不保存目前 secret。`check` 只驗資料與 adapter 是否存在，不花 token、
不偷偷 load model；`unload` 是明確操作，engine 不支援時清楚回 `not_supported`。

改 temperature 等 request parameter，只影響下一次 LLM instruction。改 engine／endpoint／model 是 model
switch：必須沒有 in-flight instruction 與 pending tool call。command 先處理舊模型 lifecycle，再 atomic
commit 新 config；新模型到下一次 LLM call 才載入。switch 與 `llm unload` 全程持有 Bot worker lock，
所以 concurrent `run continue`／`run next` 不能在中途啟動。

- direct Ollama adapter 在 `set` 回 0 前必須成功 unload 舊 model；失敗或被 Ctrl-C 時保留舊 config。
- 不支援 unload 的 remote adapter 清楚顯示 `unload: not_supported`，再切 config；不假裝遠端已卸載。
- `llm unload` 不改 config，下次 LLM call 仍可重新載入同一 model。
- 預設最多等待目前 LLM `timeout`；`--no-wait` 取得不到 exclusive lock 就立刻回 `model_busy`。

不同 Bot 可能共用 local model。每次 LLM call 對 `(engine, endpoint, model)` 持 shared Linux lock；unload
取得 exclusive lock後才執行，避免 A 切模型時卸掉 B 正在使用的模型。這只保護同 UID、經 AgentOS 發出
的 active calls；無法約束其他程式直接呼叫 Ollama。paused Bot 日後需要時可以重新 load，不算 active use。

lock filename 是 [JCS](JSON.md) `[engine, endpoint, model]` 的 SHA-256，不把 URL 字元直接當檔名。根目錄
優先用 `$XDG_RUNTIME_DIR/agentos/models/`；缺少時用 `/tmp/agentos-$UID/models/`，啟動時拒絕 symlink，並
驗證目錄由目前 UID 擁有且 mode 0700。不需要 root daemon，也不能共用別人的 lock directory。

## messages

`./messages` 輸出 Bot 真正會送給 LLM 的 canonical JSON array；細節見
[`MESSAGES.md`](MESSAGES.md)。system prompt 就是第一則 system message，不另造第四個成員。

raw 設定可用 `$ref` 拆出 system／初始 context；runtime 產生的 user、assistant 與 tool entries 一律寫在
Bot 自己的 local history，不回寫外部 ref。公開 member 負責把兩者組成 effective array，因此 Python 與
C++ 不可各自猜不同合併方式。

```sh
./messages
./messages raw
./messages check
./messages use ../shared/system-message.json ../shared/context-message.json
./messages set system "Reply briefly."
./messages unset system
./messages clear
./messages clear --all
```

追加 active Run 的要求使用 `run send`，不提供任意 index edit。真的需要手改時，日後再提供 export／
import + full validation，不把 JSON surgery 當第一版日常 API。

## tools

`./tools` 輸出完整 effective tool specs；`./tools raw` 保留 refs。v1 沿用 FreePy 已驗證的 `tooljson`
外形，不重新發明「模型看到的 schema」與「實際怎麼跑」如何綁在一起：

```json
{
  "type":"function",
  "function":{
    "name":"read_file",
    "description":"Read a text file.",
      "parameters":{
        "type":"object",
        "properties":{"path":{"type":"string"}},
        "required":["path"],
        "additionalProperties":false
      }
  },
  "_extra":{
    "_version":"0.1.0",
    "_type":"exec",
    "exec":["../read-file"],
    "argv":{"path":{"position":1}},
    "stdin":null,
    "cwd":null,
    "timeout":60,
    "stdout":{"clip":"head", "max_bytes":65536},
    "stderr":{"mode":"merge"},
    "ok_exit":[0]
  }
}
```

Python prototype 可重用 tooljson 的 parser／fixtures，但 AgentOS 專案必須保存自己的格式契約；最終 C++
讀同一份 JSON。v1 durable tools 只接受：

- `_type: exec`：固定 executable + argv/stdin recipe，`shell=false`；格式見 [`TOOL_SPEC.md`](TOOL_SPEC.md)，
  執行語意見 [`EXEC.md`](EXEC.md)。
- `_type: builtin`：AgentOS 自己的小型控制工具，例如 `ask_user`。

任意 Python callable 不進持久格式；HTTP 等 executor 之後以新的明確 type 加入。exec 仍有目前 Linux user
的完整權限，沒有 sandbox。auto mode 會執行使用者已加入的工具；要逐一確認就用 `start --step`。

```sh
./tools add ../shared/read-tool.json
./tools remove read_file
./tools check
```

`add` 先完整驗證再寫 local ref；名稱重複、schema 和 executor 缺一邊、未知 type 都 fail closed。`remove`
按 tool name，不要求使用者知道來源檔。pending call 尚未配對時，不能移除它所需的 tool。

raw manifests 與 composition 的精確格式見 [`STORAGE.md`](STORAGE.md)。v1 的 message/tool source 每個 ref
只展開一項；共享 array 使用 fragment 逐項加入，不發明隱含 array merge。

`agentos new` 預設加入安裝包提供的 `ask_user` builtin spec。它不是隱藏能力；`./tools` 看得到，也可在沒有
active Run 時移除。只有模型明確呼叫它，Run 才進 waiting。
