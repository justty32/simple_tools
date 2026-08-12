# exec_tools

`exec_tools` 是 `tooljson` 上面很薄的 discovery layer：從使用者明確指定的工具目錄讀取
`.specs/*.json`，再組成 `(schemas, dispatch)`。它不掃系統 `PATH`、不猜執行檔用法，也不讓
LLM 自動產生 spec。

```python
import exec_tools

schemas, dispatch = exec_tools.tools(["/opt/my-tools", "./project-tools"])
```

也可以用環境變數；分隔方式與 `PATH` 相同：

```sh
export FREEPY_TOOLS=/opt/my-tools:./project-tools
```

```python
schemas, dispatch = exec_tools.tools()
```

每個工具目錄的形狀是：

```text
my-tools/
  resize
  fetch-invoice
  .specs/
    resize.json
    invoices.json
```

spec 使用 [`tooljson`](../llmkit/tooljson/README.md) 格式；一個 JSON 可以描述一個或多個工具。
相對的 `_extra.exec` 以 JSON 所在的 `.specs/` 為中心，因此同層執行檔通常寫成
`"../resize"`。

## 邊界

- 只有 `.specs/` 內小寫副檔名 `.json` 的直接子檔案會被讀取。
- 目錄和檔案依名稱排序；不同目錄出現同名工具時，先指定的目錄優先，像 `PATH`。
- 沒有 spec 的可執行檔不會暴露給模型，只會出現在 `scan().missing`。
- `tools()` 遇到任何壞 spec 會丟 `DiscoveryError`，避免能力集合悄悄缺一塊。
- UI 若要一次顯示全部問題，可用 `scan()` 取得有效 specs、missing executables 與 errors；單一
  壞檔不會阻止其他檔案被診斷。

```python
result = exec_tools.scan()
result.specs       # name -> tooljson.Spec
result.missing     # 還沒有任何 spec 指到的可執行檔
result.errors      # Problem(path, message)
```

`spec_path(executable)` 會回傳慣例位置 `.specs/<檔名>.json`，只負責算路徑，不會寫檔。

## 它不保證什麼

discovery 只回答「使用者宣告了哪些能力」，不是 permission 或 sandbox。真正執行仍走
`tooljson`：exec 類型不經 shell、會套用 spec 的 timeout／參數 limits／輸出裁切，也可接
approver；但被允許的程式仍具有目前 process 的作業系統權限。需要隔離時應使用容器或 VM。

自動讀腳本、呼叫 LLM 產 spec 的 `describe` 仍延後；它牽涉成本、幻覺、人工確認與快取更新，
不屬於這個確定性的 discovery package。

## 驗證

在 `freepy/` 執行：

```sh
PYTHONPATH=llmkit uv run python -m exec_tools
```

測試完全離線，只建立暫存目錄，不會執行掃到的 fixture。
