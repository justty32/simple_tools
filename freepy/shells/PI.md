# 用 Pi 操作 FreePy agentloop

這個 launcher 讓 Pi coding agent 成為 FreePy agentloop 的操作介面。Pi extension 經
JSONL 啟動一個 Python bridge；真正的 bot、tools、Handle 與 runner 都留在 Python
process。Pi 不接管 agentloop，也不把自己的模型 endpoint 借給 FreePy bot。

## 先決條件

- Python 3.11 以上；建議安裝 [uv](https://docs.astral.sh/uv/)，並先在 `freepy` 執行
  `uv sync` 建立 `.venv`。
- Node.js 與 Pi coding agent。npm 安裝方式如下；安裝後先確認 `pi --version` 可執行：

  ```bash
  npm install --global @earendil-works/pi-coding-agent
  pi --version
  ```
- 一個受信任的 Python factory，格式為 `module:function`。Factory 可不收參數，或收一個
  JSON-compatible config，並回傳 `(bot, dispatch)`。

先用 repository 內建的離線 factory 最容易確認整條控制路徑：

```python
# adapters/pi/examples/minimal_factory.py 的介面摘要
def create(config=None):
    return bot, {"echo": echo}
```

這個 factory 是 deterministic fixture，不呼叫真實模型或網路。

## 一次啟動

先進入 repository 的 `freepy` 目錄。PowerShell：

```powershell
cd C:\path\to\simple_tools\freepy
uv sync
$env:AGENTLOOP_PI_FACTORY = "adapters.pi.examples.minimal_factory:create"
uv run python -m shells pi --offline
```

POSIX shell：

```bash
cd /path/to/simple_tools/freepy
uv sync
AGENTLOOP_PI_FACTORY=adapters.pi.examples.minimal_factory:create \
  uv run python -m shells pi --offline
```

`AGENTLOOP_PI_FACTORY` 同時指定 bridge factory 並選擇啟用整合。只要它有值，launcher
就會自動在 Pi 參數前加入 repository 內的
`adapters/pi/pi-agentloop.ts`，不用再手寫 `-e`。使用絕對 extension path，所以從
其他目錄呼叫 launcher 也不會找錯檔案。

未設定 factory 時，以下命令仍是原本的裸 Pi pass-through，不會偷偷載入 FreePy
extension：

```bash
python -m shells pi --offline
```

所有額外 Pi 參數會原樣傳遞，例如：

```bash
python -m shells pi --offline --no-session
python -m shells pi --offline --model <pi-model>
python -m shells pi --offline --continue
```

若已自行用 `-e` 或 `--extension` 傳入同一份 bundled extension，launcher 不會重複
加入。也可以完全繞過 launcher，從 repository root 手動啟動：

```bash
AGENTLOOP_PI_FACTORY=adapters.pi.examples.minimal_factory:create \
  pi --offline -e freepy/adapters/pi/pi-agentloop.ts
```

## 在 Pi 裡操作一個 Round

載入成功後，輸入下列 slash commands：

```text
/al-start <prompt>     啟動一個 Round
/al-status             顯示目前 snapshot
/al-wait [seconds]     等待下一個 boundary event，預設 10 秒
/al-pause              要求在下一個安全邊界暫停
/al-resume [prompt]    可先替換 prompt，再繼續
/al-end [reason]       在安全邊界結束 Round
```

第一次可照這段完整走一次：

```text
/al-start demo
/al-status
/al-resume
/al-wait 5
/al-status
/al-end done
```

離線 factory 預設在每個 `after_step` gate 暫停，所以 `/al-start demo` 後看到
`state: "paused"` 是預期行為。`/al-resume` 讓 runner 繼續；`/al-wait 5` 最多等五秒，
沒有新 event 時只會 timeout 返回 snapshot，不會永遠卡住。`/al-pause` 也不會硬切斷正在
執行的模型 request 或 Python tool，而是在下一個 agentloop safe boundary 生效。

`/al-resume 新提示` 會先更新 prompt，再 resume。`/al-end done` 是安全結束請求；Pi
session 關閉時 extension 也會嘗試用 `pi_shutdown` 結束 bridge，等待短暫 grace period。

Pi 模型同時會看到 `agentloop_control` tool，可使用
`start/status/wait/pause/resume/end/edit/join` actions。人類 slash commands 與 Pi 模型
共用同一個 bridge client；不要把它們當成兩份 Round。

## 換成真實 factory

真實專案應自行寫 factory，明確決定 bot、模型 preset、tools、workspace 與 Limits：

```python
def create(config=None):
    bot = make_bot(config or {})
    dispatch = {
        "read_file": read_file,
        "write_file": write_file,
    }
    return bot, dispatch
```

設定值必須是可由 bridge process import 的 `module:function`：

```powershell
$env:AGENTLOOP_PI_FACTORY = "my_project.pi_factory:create"
uv run python -m shells pi
```

Factory 是受信任 Python code，bridge 不是 sandbox。檔案 root、shell approval、模型成本、
timeout 與工具權限仍由 factory／runtime 負責；不要讓 Pi tool payload 指定任意 import
path。

這裡有兩個彼此獨立的模型角色：

1. Pi 自己的模型，負責 Pi 對話及是否呼叫 `agentloop_control`。
2. Factory 建立的 FreePy bot，負責被控制的 agentloop Round。

內建 `minimal_factory` 沒有真模型，因此 bridge、commands 與 RPC 驗證完全不依賴
Ollama。換成真實 factory 後，只有該 factory 所選 endpoint 必須可用；公司 Ollama、家用
LiteLLM 或其他 endpoint 都不應被 launcher 暗自假設。

## 離線與 RPC 驗證

從 `freepy` 目錄先跑兩項不需 Pi 模型的檢查：

```powershell
uv run python -m shells._checks_pi
uv run python adapters/pi/check_pi_bridge.py
```

第一項驗證 factory opt-in、自動 extension path、參數 pass-through 與去重；第二項以
subprocess 驗證 `start → pause → edit → resume → end → join` 的 JSONL bridge protocol。
兩項都不呼叫 Ollama 或任何模型。

若還要確認本機 Pi 真的載入 extension，可用 RPC mode 查 commands。先設定離線 factory，
再執行：

```powershell
$env:AGENTLOOP_PI_FACTORY = "adapters.pi.examples.minimal_factory:create"
'{"id":"probe","type":"get_commands"}' |
  uv run python -m shells pi --offline --approve --mode rpc --no-session
```

```bash
printf '%s\n' '{"id":"probe","type":"get_commands"}' |
  AGENTLOOP_PI_FACTORY=adapters.pi.examples.minimal_factory:create \
  uv run python -m shells pi --offline --approve --mode rpc --no-session
```

結果的 `data.commands` 應包含 `al-start`、`al-status`、`al-wait`、`al-pause`、
`al-resume`、`al-end`。stdin 在 probe 後結束，Pi RPC process 也應正常退出。`--approve`
代表本次信任 repository 內明確載入的 extension；只在你信任這份工作樹時使用。

## Windows 注意事項

- 建議從 PowerShell 執行；環境變數用 `$env:NAME = "value"`，只影響目前 shell。
- Extension 會優先找 `freepy/.venv/Scripts/python.exe`。若沒有 `.venv`，Windows 通常沒有
  `python3` 命令，請先 `uv sync`，或明確設定：

  ```powershell
  $env:AGENTLOOP_PI_PYTHON = (Get-Command python).Source
  ```

- `AGENTLOOP_PI_PYTHON` 應指向能 import 本專案依賴的 Python。Launcher／extension 會補上
  repository root、`freepy` 與 `freepy/llmkit` 的 `PYTHONPATH`。
- 不要把 bash 的 `NAME=value command` 語法直接貼進 PowerShell。

## 疑難排解

`找不到 pi，裝了嗎？`
: `pi` 不在目前 `PATH`。另開終端後先跑 `pi --version`；若使用 Node version manager，
  確認該 shell 已啟用安裝 Pi 的 Node 環境。

Pi 裡沒有 `/al-start`
: 先確認 `AGENTLOOP_PI_FACTORY` 在啟動 launcher 的同一個 shell 有值。再跑上述 RPC probe；
  若手動傳 `-e`，確認 path 指向 `freepy/adapters/pi/pi-agentloop.ts`。Pi extension API 可能隨
  版本變動，也應記錄 `pi --version`。

`no factory configured`
: Extension 已載入，但 child process 沒收到 factory。檢查變數名稱與值是否為
  `module:function`，並避免在 Pi 啟動後才設定環境變數。

`cannot load factory ...`
: Module 不可 import、function 名稱錯誤，或 Python interpreter 不對。先在 `freepy` 執行
  `uv run python -c "from my_project.pi_factory import create"`；Windows 也檢查
  `AGENTLOOP_PI_PYTHON`。

RPC probe 沒有 commands 或卡住
: 加上 `--offline --no-session`，並給 stdin 一行合法 JSON 後關閉。先單獨跑
  `shells._checks_pi` 與 `check_pi_bridge.py`，即可分辨是 launcher／bridge 問題，還是 Pi
  extension 載入問題。

`/al-start` 後停在 paused
: 離線 factory 的 `after_step` gate 本來就會 pause；用 `/al-status` 確認後執行
  `/al-resume`。這不是模型 endpoint 失聯。

想深入理解 edit allowlist、event sequence、process lifecycle 與安全邊界，見
[`docs/freepy/adapters/pi/`](../../docs/freepy/adapters/pi/README.md)。
