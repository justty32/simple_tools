# `freepy` 目前的權限邊界

本專案已經把「agent 迴圈預算」與「作業系統資源」分開思考，方向正確。目前真正完成的是應用層限制；OS sandbox 仍在規劃階段。

## 已經有效的限制

`agentloop.Limits` 能限制迴圈自己看得到的事件，例如：

- 回合、工具呼叫與時間預算。
- token 使用量與可用模型。
- 工具白名單與單一工具呼叫次數。

這些限制可靠，因為每次工具呼叫都必須經過 `agentloop` 的 `_settle()`。達到預算時可以不執行工具，或把拒絕原因回傳模型。

`base_tools` 的檔案工具也會將路徑 resolve 後確認仍位於 workspace root，能阻擋一般的 `..`、絕對路徑與 symlink 越界。這是檔案工具 API 的有效邊界。

## `run_shell` 沒有檔案邊界

[`base_tools/shell.py`](../../freepy/base_tools/shell.py) 使用：

```python
subprocess.run(command, shell=True, cwd=str(get_root()), ...)
```

`cwd` 只指定子程序從哪裡開始，不限制它之後可開啟的路徑。命令仍可讀寫宿主使用者有權存取的任何檔案，也可啟動更多子程序與建立網路連線。

`approver` 可以避免誤操作，但 literal command 檢查或黑名單能被變數、腳本、替代命令與新執行檔繞過，因此不應被描述成 sandbox。

## `_type: exec` 防注入但不隔離

[`tooljson/invoke.py`](../../freepy/llmkit/tooljson/invoke.py) 使用 argv list 與 `shell=False`。模型提供的 `;`、`$(...)` 等字元不會再交給 shell 解讀，這確實縮小 command injection 風險。

但成功啟動的 executable 仍繼承目前使用者的檔案、網路與 process 權限。`shell=False` 解決的是「參數如何解讀」，不是「程序可以存取什麼」。

## Python 工具與同 process agent

`agentloop` 透過 `asyncio.to_thread()` 呼叫 dispatch 中的 Python function。這些函式仍在 agentloop 所在 process 裡，只是換到 thread 執行。

因此：

- 對它們設定 per-process `rlimit` 會同時限制 supervisor 自己。
- 同 process 中無法讓 agent A 看到目錄 A、agent B 只看到目錄 B。
- Python 工具可以直接使用 `os`、socket 或第三方函式庫，繞過 `base_tools` 的路徑檢查。
- 任一工具洩漏或破壞 process global state，其他 agent 也會受到影響。

若 dispatch 只包含經過審查的可信任函式，這可以是合理設計；但不可把它宣稱為 hostile-code isolation。

## `rlimit`、cgroup 與容器各自負責什麼

| 機制 | 能限制 | 不能可靠限制 |
|---|---|---|
| `rlimit` | CPU 秒數、address space、open files、process 數 | 檔案路徑、網路目的地、真正 CPU 比例 |
| cgroup v2 | CPU、memory、PID、部分 IO | 檔案視野、網路 allowlist、secret |
| mount/net/user namespace 或容器 | 檔案視野、網路、身分、capability | 自動判斷允許目的地上的資料是否可外傳 |

所以 [`agentloop/LIMITS.md`](../../freepy/agentloop/LIMITS.md) 提議先做 `rlimit` 並明說其限制是正確的；但它只保護外部子程序的資源使用，不能解決本次關心的完整存取控制。

## 風險分級

1. **低風險**：只提供固定、狹窄、經過路徑驗證的 Python 工具，不提供 shell/exec。
2. **中風險**：提供固定 executable 與受控 argv，但它仍有宿主權限。
3. **高風險**：讓模型產生任意 shell command，或載入未審查的 Python tool。
4. **不同信任域**：多個客戶、agent 或任務彼此不可互信；必須拆成不同 process/container。

目前 `run_shell` 與任意 `_type: exec` 應按高權限工具看待。workspace root、approval 與 timeout 都值得保留，但它們應放在 OS sandbox 之外作為額外防線。

