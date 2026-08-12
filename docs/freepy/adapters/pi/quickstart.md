# Pi agentloop quickstart

[← 返回 Pi adapter 總覽](README.md)

這個範例使用 Pi extension 啟動 Python bridge，再由 bridge 在背景 thread 執行
agentloop。Pi 只收發 JSON snapshot；真正的 Handle、bot 與 Python tools 都留在
bridge process。

## 離線驗證

從 `freepy` 目錄執行：

```bash
uv run python adapters/pi/check_pi_bridge.py
```

這會用 `adapters/pi/examples/minimal_factory.py` 驗證：

```text
start → after_step pause → edit tool call → resume
      → after_tools event → final after_step pause → end → join
```

全程不呼叫真實模型或網路。

## 在 Pi 載入 extension

從 repository root 執行：

```bash
AGENTLOOP_PI_FACTORY=adapters.pi.examples.minimal_factory:create \
pi -e freepy/adapters/pi/pi-agentloop.ts
```

`-e` 適合開發與測試。要在某個專案自動載入，可把 extension 複製或 symlink
到該專案的 `.pi/extensions/`；這個 repository 不會自動替使用者安裝。

已驗證 Pi 0.84.1 可載入這份 extension。以後版本若 API 變動，應以 Pi 官方
extension 文件為準。

## Slash commands

```text
/al-start <prompt>     啟動一個 Round
/al-status             顯示當前 snapshot
/al-wait [seconds]     等下一個 boundary event
/al-pause              在下一個安全邊界暫停
/al-resume [prompt]    可選地設定 prompt，再繼續
/al-end [reason]       安全結束 Round
```

Pi 模型也會看到 `agentloop_control` tool，可使用同樣的
`start/status/wait/pause/resume/end/edit/join` actions。人類 commands 與模型 tool
共用同一個 bridge client。

離線 factory 的預設 gate 會在每個 `after_step` 暫停。一段手動流程可以是：

```text
/al-start demo
/al-status
/al-resume
/al-wait 5
/al-status
/al-end done
```

## Factory contract

Bridge 不允許 Pi 傳入任意 Python import path。Factory 由 bridge process 啟動前通過
`AGENTLOOP_PI_FACTORY` 或 `--factory` 指定，格式為 `module:function`。

Factory 可以不收參數，或收一個 JSON-compatible config；回傳 `(bot, dispatch)`：

```python
def create(config):
    bot = make_bot(config)
    dispatch = {
        "read_file": read_file,
        "write_file": write_file,
    }
    return bot, dispatch
```

真實專案應由 factory 自行限制模型、tools、檔案 root 與 Limits。Bridge 只限制
遠端可修改的 Handle 欄位，不是 sandbox。

## Bridge 設定

- `AGENTLOOP_PI_FACTORY`：受信任的 `module:function` factory。
- `AGENTLOOP_PI_PYTHON`：可選的 Python executable；未指定時優先使用
  `freepy/.venv`，再退回 `python3`。

Extension 會自動把 repository root、`freepy` 與 `freepy/llmkit` 加入 child process
的 `PYTHONPATH`。Bridge stdout 只用於 JSONL；factory、bot 與 tool 的普通 `print()`
會被導向 stderr。

## Edit 邊界

遠端 `edit` 目前只允許整體替換：

```text
prompt, images, ask_options, tool_calls, tool_results, auto_finish
```

可傳 `expected_step` 與 `expected_state`；不符時 bridge 會拒絕過時修改。修改不會
自動 resume。

## 生命週期

Pi `session_shutdown` 時，extension 會要求 bridge `end(reason="pi_shutdown")` 並等待
短暫 grace period。agentloop 不會強制切斷已開始的 Step 或 tool batch；若 Pi 結束
時 operation 仍未返回，extension 最後只能將 child process 視為異常收尾。
