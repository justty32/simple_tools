# agentloop 與多 agent 決策筆記

本頁保存 2026-08-09 至 2026-08-10 的實作理由；目前 API 仍以
[`agentloop/README.md`](../agentloop/README.md) 為準。

## 第四層落地：`agentloop`

四層疊完後，`agentloop` 對外只有阻塞 `run()` 和 thread-safe `Handle`。它不建 task、不開
thread，也不提供 async 版本；需要背景執行時由上層 runtime 放進 worker。

控制指令一律在下一個安全邊界生效，沒有假裝能立即切斷 HTTP 或跑到一半工具的 `cancel()`。
否則呼叫端可能誤以為工具沒有產生副作用。

### 先還工具債，再走下一步

最初工具放在迴圈尾端執行：工具副作用已發生，預算卻可能在結果送回模型前耗盡；再次
`run()` 便會重做工具。

修正後，每步開頭先結算上一則 message 欠的 tool calls，尾端只記下 pending debt。若預算先
用完，工具尚未執行，債留在 `bot.history`；同一 bot 再跑便自然續接。續跑不是另一套分支，
而是安全排序的結果。

### 限制有兩種結果

- 預算真的耗盡：停止整個 agent，例如步數、時間、token、總呼叫數或 engine 限制。
- 只有這個工具不可用：回一則 tool result 給模型，例如白名單或 per-tool 次數。

第二種是情報，不是整體錯誤。模型知道 `run_shell` 已用滿後仍可改用其他工具。這延續了
「工具回傳值永遠是字串」的協定。

CPU、GPU、記憶體、網路和檔案不屬於這種合作式計數；規劃見
[`agentloop/LIMITS.md`](../agentloop/LIMITS.md)。沒有真正 enforcement 的欄位不能放進 API，否則
使用者會誤以為 agent 已被隔離。

### `quiet` 是完成條件的一般化

原本的「一不叫工具就結束」其實是 `quiet=1`。調大後，loop 會用 nudge 再給模型幾次動手
機會，處理小模型只描述計畫、不使用工具的情況。代價是模型真的完成時也會多燒步數，
所以預設仍是 1。

### 離線驗證

`FakeBot` 繼承真正 `LLM`，只把 `ask()` 換成依腳本產生 `Reply(假 response)`；history、
pending calls 和 calls 都走正式路徑。競態測試用 `threading.Event` 固定 instruction/completion、
pause/stop 和 model/tool 同時到達的時點，不依賴碰運氣的 sleep。

## 多 agent 規劃拆層

原本 `team_tools/PLAN.md` 只有 mailbox，名稱卻暗示已處理組織與授權，因此拆成：

- [`communication_tools`](../../docs/freepy/future/communication-tools/PLAN.md)：直接傳訊與原子 mailbox，不知道上下級。
- [`team_tools`](../../docs/freepy/future/team-tools/PLAN.md)：組織樹、grant、allocation、task/report；canonical path 不自動等於權限。
- [`agent_runtime`](../../docs/freepy/future/agent-runtime/PLAN.md)：spawn/fork/lifecycle；child 權限只能縮小，consumable
  budget 必須 reserve，受限 agent 不拿 Podman socket。
- [`memory_tools`](../../docs/freepy/future/memory-tools/PLAN.md)：無損卸載 tool result；同一 resolver 支援 JSON `$ref`
  與 Markdown link，但 ref 不是授權。
- [`introspection_tools`](../../docs/freepy/future/introspection-tools/PLAN.md)：唯讀回答身份、能力與剩餘預算。

[`agentloop/ROUNDS.md`](../agentloop/ROUNDS.md) 固定兩層時間：一次 `ask() → message` 是 Step；模型從
指令啟動、經過多步與工具、最後停止是 Round。工具向使用者取資料仍是 tool call 內部事件。

機器資源仍需外層 enforcement：rlimit 只管得到子行程，網路和檔案需要 namespace／容器，GPU
只能容易地限制可見裝置，不能冒充 per-process 顯存硬上限。
