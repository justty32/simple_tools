# 現有 freepy 模組評估

| 模組 | 現況 | 等級 | 建議 |
|---|---|---:|---|
| `llmkit/tooljson` | spec、registry、`exec`／`python`、argv、clip 已實作 | A/B | 第一個 Janet slice；`python` type 留 adapter |
| `llms` presets | id → endpoint/model/parameters 的 JSON object | A | 普通資料 lookup；不需獨立固化層 |
| `agentloop` | Step loop、Limits、Handle、pause/stop、usage | B | Janet 狀態機；I/O 經 adapter，重新驗競態 |
| `llmkit/llms` | OpenAI SDK、history、streaming、tool calls、vision | B/C | protocol/state 可固化；transport 暫留 proxy/Python |
| `base_tools` | read/write/edit/shell、root containment | B/C | 檔案規則可移；process 與安全留 OS backend |
| `prototypes/exec_tools/discover` | `FREEPY_TOOLS`、spec scan、missing/errors 的草稿 | A/B | 純掃描可移；先凍結規則，describe 的 LLM 仍是 adapter |
| `shells` | chdir、PYTHONPATH、`execvpe` 薄殼 | C/D | 不值得逐行移植；按部署重做 launcher |
| prototypes／`examples/llm_tool_roundtrip.py` | 互動實驗與 demo | D | 繼續 Python 探索，不列固化承諾 |

## `tooljson`：最佳起點

它的文件已明說「規格才是產品，Python package 是第一個實作」。外殼只保留 `_version`、`_type`，body 由 registry 擴充；這很適合做語言中立的 algebra。

Janet 第一版應做：

- JSON parse／strict validation。
- `exec` 的 positional、switch、option、repeat、stdin 映射。
- deterministic ordering、`ok_exit`、limits、output clipping。
- registry 與乾淨 OpenAI tool schema。

不應搬的部分：`_type:"python"` 的 module/attr import、Python signature/docstring reflection。可新增 Janet body type，但跨語言主路徑應優先使用 `exec`。

## `agentloop`：移植語意，不移植 async 寫法

目前核心很乾淨：bot 只需 `ask()`、`pending_calls`，工具是 dispatch table。Step、pending call debt、quiet、budget 與 stop reason 都能以 Janet table + state transition 表達。

但 `asyncio.to_thread`、`threading.Event` 不能機械翻譯。Janet 的 fiber/channel 是不同模型；Round 追加指令的 completion/enqueue lock、tool-input timeout、cancel、stop 喚醒必須重新以 protocol test 定義。

## `llms`：拆成兩半

適合 Janet：message/history、tool-call accumulation、finish reason、usage、rollback、capability decision。

暫不適合：Python OpenAI SDK 與 provider 差異、HTTPS、SSE streaming。`langlab-janet` 已證明本機 OpenAI-compatible HTTP 與多步 tools 可行，但 `spork/http` 沒 TLS 和串流；近期應走 LiteLLM local proxy。

## `base_tools` 與 containment

read/write/edit 是普通 bytes/path transformation，可固化。`Path.resolve()`、symlink、case、Windows drive、UNC、POSIX mode 都要以跨平台 fixtures 明定。

root containment 不是 sandbox；`run_shell` 本來就可執行工作區外的程式。是否能讀網路、看宿主檔案或耗盡資源，仍由 namespace/container/cgroup 決定。

## `llms` presets

資料刻意只有 id、endpoint、model、parameters 和可選 description。Janet 若需要同一份
設定，直接讀 JSON object 即可；不建立 model metadata、來源或 capability 系統。

## 實跑反映的現況

- `agentloop` 35 關全過；是可信的 reference trace 來源。
- preset loader 只需驗 JSON lookup 與 Engine mapping，不再有獨立測試套件。
- `base_tools` 在 Windows 的檔案路徑關卡過，POSIX shell 關卡按設計失敗。
- `tooljson` 的 pure validation/argv 多數過，但 exec 測試使用 POSIX 腳本，Windows 只得 32/45。

這些失敗不是否定 Janet，而是提醒：process ABI 與平台行為必須留在架構邊界。

## 來源

- [tooljson 格式](../../../../freepy/llmkit/tooljson/FORMAT.md)
- [agentloop](../../../../freepy/agentloop/README.md)
- [base_tools](../../../../freepy/base_tools/README.md)
- [llms presets](../../../../freepy/llmkit/llms/README.md#preset)
- [exec_tools 計畫](../../../freepy/future/exec-tools/PLAN.md)
