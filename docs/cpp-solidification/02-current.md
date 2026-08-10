# 現有 freepy 模組評估

## 總表

| 模組 | 現況 | C++ 評級 | 建議 owner |
|---|---|---:|---|
| `llmkit/tooljson` pure／exec | spec、strict load、registry、argv、subprocess、clip | A/B | C++ core；process 為 native adapter |
| `tooljson _type:"python"` | importlib、sys.path/sys.modules、任意 callable | D | Python sidecar；跨語言走 exec／RPC |
| `llms` presets | id → endpoint/model/parameters 的 JSON object | A | 簡單 config loader；不需獨立 subsystem |
| `agentloop` | Step loop、budget、pending debt、pause/stop | B | C++ reducer + scheduler adapter |
| `llmkit/llms` protocol/state | history、tool calls、stream accumulation、usage、caps | B | 可固化 value/state |
| `llmkit/llms` transport/reflection | OpenAI SDK、HTTPS/SSE、inspect/type hints/docstring | C/D | proxy/Python adapter；schema 改顯式契約 |
| `base_tools` | files/edit/root containment/POSIX shell | B | C++ filesystem core + Linux process adapter |
| `exec_tools/discover` | env path scan、spec lookup、missing/errors | A/B | C++；規則先凍結 |
| `shells` | chdir、PYTHONPATH、`execvpe` launcher | D | 依部署重做，不逐行移植 |
| prototypes／`try.py` | 互動探索與 demo | D | 繼續 Python |
| LiteLLM proxy | provider compatibility、YAML、FastAPI/LiteLLM | C | 保留外部 service，不 fork 重寫 |

## `tooljson`：最佳第一切片

`FORMAT 0.1.0` 已明說「規格才是產品」，而且外殼只認 `_version`／`_type`，很適合以 C++ 做第二實作。優先搬：

- strict JSON load、同檔撞名與 deterministic ordering。
- `_type` registry 介面。
- exec positional／option／switch／repeat／stdin 的 argv decision。
- fingerprint、stale、exit policy、decode／clip。

不要假裝能語意等價搬 `_type:"python"`。現況會動態載入檔案或 package、修改 `sys.path`／`sys.modules`、執行 module、找 callable，再以 `**arguments` 呼叫。合理邊界是 Python sidecar，或把工具改寫成 `exec`／stdio RPC。

open registry 也要重新定義。第一版可允許 application 在 startup 註冊 C++ handler；第三方擴充優先用 process protocol。不要一開始載入任意 C++ shared object：C++ ABI、allocator、exception 與 unload lifecycle 都會變成事故面。

## `llms` presets：保持為設定，不升格成 subsystem

`llms/presets.json` 只把 id 映到 endpoint、model、parameters 和可選 description。若未來
C++ client 需要它，只需 JSON lookup 與最小形狀檢查；capability、研究來源、alias/mode
推導都不屬於 preset，也不值得為它建立 native value/store core。

## `agentloop`：應固化狀態，不照搬 `asyncio`

值得搬的是：pending tool debt、round/call/token/time budget、quiet、finish reason、resume 與 ordered tool settlement。建議先表達成：

```text
reduce(RoundState, Event) -> RoundState + Intents
```

HTTP request、tool execution、timer 與 user control 由 host/scheduler 產 event。不要把 `asyncio.to_thread` 機械翻成 coroutine；C++ coroutine 並不自動給你 deadline、cancellation 或 thread safety。

現況還有一個必須先釘死的缺口：`Handle` 文件說別的 thread 可讀／控制，但 fields、`steps`、`_say`、completion 都沒有 lock。Python smoke 在單 event-loop/GIL 條件過關，不足以證明跨 thread 線性化。C++ 版需要 mutex/condition 或 single-owner event queue，並明定 completion/enqueue 原子邊界。

`calling.py` 依 `inspect.signature` 先 bind 任意 callable。C++ core 應改讀 `ToolDescriptor`／JSON schema，dispatch 經 typed handler 或 RPC；不要模擬 Python reflection。

## `llms`：transport 不應成為第一戰

message/history、tool-call fragment accumulator、rollback、finish/usage normalization 都適合 C++。但現有 OpenAI SDK 幫忙吸收了 HTTPS、SSE、response object 差異與 provider 行為。直接重寫會讓小型 semantic migration 膨脹成 networking product。

近期保留 LiteLLM local proxy，C++ 只實作一個受控的 OpenAI-compatible client；甚至第一階段完全由 Python transport 送入 normalized events。Python `func_schema.py` 的 signature/type hint/docstring reflection 則保留作 schema producer，runtime 一律消費落盤 JSON。

## `base_tools` 與 process

path resolve、root containment、bytes/text clipping、exact edit 都適合 C++ filesystem value／adapter。`run_shell` 明確只支援 POSIX；Windows 現在 fail closed。C++ 版不該為了「跨平台」暗中改語意：

- pure path/file contract 兩平台都跑。
- shell/process production 先 Linux-first。
- argv-based exec 與 shell command 分開。
- sandbox 仍由 namespace/container/cgroup/seccomp 提供；`std::filesystem` containment 不是 sandbox。

## 現有實跑

| 平台 | agentloop | tooljson | base_tools |
|---|---:|---:|---:|
| Windows | 35/35 | 32/45 | 20/26 |
| WSL Ubuntu | 未重跑 | 45/45 | 全過 |

Windows 的 13 + 6 個失敗都落在 POSIX executable/shebang/shell 邊界。這些是 native port 的平台 contract 證據，不應被「修」成 Windows 上任意選一個 shell。

現有驗證都是 module smoke，還沒有 pytest/unit coverage、golden corpus、fuzzer 或 sanitizer。它們足以當 oracle 素材，不足以直接批准 default 切換。
