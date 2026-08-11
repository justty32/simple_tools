# agentloop.limits

`agentloop.limits` 是 agentloop 內建的一套控制策略。它提供常用的 Step、tool、時間與
token 限制，但不是 agentloop 核心控制流的一部分。

它的定位是：

- 想快速使用常見限制的人，可以直接使用 `Limits`。
- 想增加自己規則的人，可以在 `Limits` 旁邊組合 callbacks，或基於它擴充。
- 完全不需要這套規則的人，可以不使用 `Limits`，直接用 callbacks 建立自己的控制流。

沒有任何自訂 policy 必須繼承的抽象基底；`agentloop.run()` 也沒有 `limits=` 特例。

## 直接使用內建 Limits

```python
import agentloop
from agentloop.limits import Limits

h = agentloop.Handle()
Limits(
    steps=20,
    calls=40,
    per_tool={"run_shell": 5},
    tools=["read_file", "run_shell"],
    engines=["deepseek-chat"],
    seconds=300,
    input_tokens=200_000,
    output_tokens=20_000,
).attach(h)

agentloop.run(bot, dispatch, "開始", handle=h)
```

`attach()` 實際上只做這件事：

```python
handle.after_step.append(limits.after_step)
```

所以 Limits 使用的能力與使用者 callback 完全相同，不擁有隱藏控制入口。

## 每個欄位的意思

| 欄位 | 意思 | 到達限制時 |
|---|---|---|
| `steps` | 要求 tools 的 Step 到達此步數時停止 | `END`，原因為 `budget` |
| `calls` | 最多真正執行幾次工具 | 批次超額 call 變成錯誤 result；之後再要求工具則 `END` |
| `per_tool` | 每支工具各自的次數上限 | 移除 call，回錯誤 result 給模型 |
| `tools` | 允許使用的工具名稱 | 移除 call，回錯誤 result 給模型 |
| `engines` | 允許使用的模型名稱 | `END`，原因為 `engine` |
| `seconds` | Round 經過時間上限 | `END`，原因為 `time` |
| `input_tokens` | provider 回報的累計 input/prompt tokens | `END`，原因為 `input_tokens` |
| `output_tokens` | provider 回報的累計 output/completion tokens | `END`，原因為 `output_tokens` |

`Limits()` 預設 `steps=12`，其他項目不限。不 attach Limits，就沒有這些預算規則。

Handle 另外公開 `cached_input_tokens`，它是 `input_tokens` 中命中前綴快取的
子集。`handle.tokens` 保留為 input + output 的總量報表，不再作為 Limits 的
合併預算。這樣外部計價 policy 可以分別套用普通 input、cached input 與 output
費率；內建 Limits 不內建幣別或模型價目表。

參數在建構時驗證：次數必須是適當的整數，秒數必須是有限非負數，tool／engine 名稱
必須是非空字串。設定錯誤會在 Round 開始前直接拋出 `ValueError`。

## 為什麼有些限制會 END，有些只回 tool result

內建策略區分兩種情況：

- Round 的整體預算已經耗盡：回 `END`，直接結束 Round。
- 只有某支工具不能使用：移除該 call，寫入錯誤 result，讓模型下一步改用其他方法。

例如工具白名單拒絕 `run_shell` 後，模型會收到類似：

```text
Error: run_shell is not available for this task. You may only use: read_file
```

被 policy 擋掉的 call 不算已執行的工具。

## 基於 Limits 加上自己的控制流

最簡單的方式是組合，不必繼承：

```python
import agentloop
from agentloop.limits import Limits

def require_approval(h):
    if any(call["name"] == "delete_file" for call in h.tool_calls):
        h.end_reason = "approval"
        return agentloop.PAUSE
    return agentloop.CONTINUE

h = agentloop.Handle()
Limits(steps=20, calls=40).attach(h)
h.after_step.append(require_approval)
agentloop.run(bot, dispatch, "開始", handle=h)
```

同一邊界的 callbacks 會全部執行，後面的 callback 看得到前面的修改；最後按
`END > PAUSE > CONTINUE` 彙總。callback 的註冊順序因此也是 policy 組合順序。

若確實想以內建行為為起點，也可以繼承並覆寫公開的 `after_step()`：

```python
class ProjectLimits(Limits):
    def after_step(self, h):
        decision = super().after_step(h)
        if decision != agentloop.CONTINUE:
            return decision
        if project_budget_exhausted(h):
            h.end_reason = "project_budget"
            return agentloop.END
        return agentloop.CONTINUE
```

正式的公開擴充面是 `attach()`、`after_step()` 與建構參數；以下畫線開頭的方法是實作
細節，不應作為 subclass 契約。

## 完全不使用 Limits

使用者可以直接註冊自己的 callbacks：

```python
import agentloop

def my_after_step(h):
    if should_stop(h):
        h.end_reason = "my_policy"
        return agentloop.END
    if should_pause(h):
        return agentloop.PAUSE

    h.tool_calls = rewrite_calls(h.tool_calls)
    return agentloop.CONTINUE

def my_after_tools(h):
    h.tool_results = filter_results(h.tool_results)
    return agentloop.CONTINUE

h = agentloop.Handle()
h.after_step.append(my_after_step)
h.after_tools.append(my_after_tools)
agentloop.run(bot, dispatch, "開始", handle=h)
```

這條路可以自行決定預算、人工核准、工具改寫、result 過濾、審計、暫停或結束方式。
核心只負責依 callback 結果推進狀態機。

## 合作式限制的邊界

Limits 只在完整 Step 提交後的 `after_step` 邊界檢查，因此：

- 不會切斷已送出的模型請求；
- 不會強殺正在執行的工具；
- `seconds`、`input_tokens` 與 `output_tokens` 可能超過最後一個
  operation 的用量；
- 工具自己呼叫其他 LLM 所用的 tokens，除非回報到 Handle，否則 Limits 看不到；
- CPU、記憶體、GPU、網路與檔案系統隔離不屬於 Limits。

需要硬性資源隔離時，應由呼叫者在 process、作業系統、container、VM 或共用 LLM
proxy 層處理。這不是內建 Limits 的後續擴張方向；更精簡的責任邊界見
[上一層 LIMITS.md](../LIMITS.md)。
