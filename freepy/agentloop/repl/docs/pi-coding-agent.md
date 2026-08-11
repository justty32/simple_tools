# 以 Pi coding agent 操作 agentloop

本文記錄把 [Pi coding agent](https://github.com/earendil-works/pi/tree/main/packages/coding-agent)
當作 agentloop 進階互動入口的設計與第一版實作。
官方 API 與 package 名稱最後核對日期為 2026-08-11。

> **實作狀態：**Pi 0.84.1 已驗證能載入 TypeScript extension 與註冊 slash
> commands；Python bridge 的完整 gate、edit、resume、end、join 流程已通過離線
> subprocess smoke test。尚未使用真實 LLM factory 做端到端測試。

## Pi 是什麼，以及這裡採用哪個介面

Pi 是一個可擴充的 coding agent。它本身提供互動式終端介面、模型與 session 管理，以及
`read`、`write`、`edit`、`bash` 等 coding tools；工作流程可以用 skills、prompt templates、
TypeScript extensions 與 packages 擴充。

Pi 目前有三個值得考慮的程式化入口：

- **Extension API**：在 Pi 的互動式 TUI 裡註冊 model 可呼叫的 custom tools、人可呼叫的
  slash commands、事件與 UI。官方文件說 extension 會從 `.pi/extensions/` 等位置載入。
- **SDK**：在 Node.js／TypeScript process 直接建立及控制 `AgentSession`。
- **RPC mode**：以 `pi --mode rpc` 啟動 headless process，透過 stdin/stdout 的 JSONL
  commands、responses 與 events 操作；官方特別把它定位為跨語言與 process-isolated 整合。

本專案第一版採用 **Pi 互動式 TUI + project-local TypeScript extension + Python bridge child
process**。Extension API 是人與 Pi 模型的入口；Python bridge 才是 agentloop 的 controller。
Pi RPC 暫不作為第一版，因為它會拿掉現成 TUI，反而要求我們再造一層 host UI。SDK 也不會
消除 Node.js 與 Python 之間的 process 邊界。

> 名稱與 package scope 可能隨 Pi 發行版變動。實作時應依鎖定版本的官方文件與 export
> 檢查；本文使用目前官方 repo 文件中的 `@earendil-works/pi-coding-agent` 名稱。

## 整合目標

- 人可以繼續使用 Pi 的終端介面，讓 Pi 幫忙觀察、暫停、修改、恢復或結束一個 Round。
- Pi 模型與人使用同一條公開 bridge protocol，不替任何一方建立隱藏控制入口。
- agentloop 核心不認識 Pi；adapter 只組合 `Handle`、callbacks、`agentloop.threading.start()`。
- 保留 agentloop 的 Step／tool batch 安全邊界，尤其是 tool 執行前與結果提交後的介入點。
- Handle、bot、Python callables 與 lock 永遠留在擁有 runner 的 Python process。
- 第一版足以用真實任務驗證公開 API，而不是先建完整的多-agent runtime。

## 非目標

- 不把 Pi 的 session、messages 或 tools 假裝成 agentloop Handle 的同一份狀態。
- 不讓 Handle 跨 process 傳遞，也不嘗試序列化 `bot`、`dispatch` callables、lock 或 exception。
- 不讓 Pi 直接強制中斷正在執行的 model request、Python tool 或 runner thread。
- 不提供 sandbox、權限隔離、scheduler、持久化 Round 或多 Handle fleet management。
- 不在第一版同步 Pi 與 agentloop 的完整對話歷史，也不自動把兩邊的 tool calls 混在一起。
- 不用 Pi RPC 的 `abort` 來模擬 agentloop 的 `pause()` 或 `end()`；兩者控制的是不同 agent。

## Process、Handle 與 thread 邊界

推薦拓撲如下：

```text
Pi process（Node.js）
  ├─ 互動式 TUI
  ├─ Pi 自己的模型迴圈與 tools
  └─ agentloop extension
       │ stdin/stdout JSONL
       ▼
Python bridge process
  ├─ controller/main thread：讀命令、序列化 snapshot、操作 Handle
  └─ runner thread：agentloop.run(...)
       └─ Handle、bot、dispatch、callbacks 全留在此 process
```

Pi extension 拿到的不是 Handle，而是一個 remote reference，例如 bridge instance id 加上
Round id。所有讀寫都轉成 JSON 命令。Python bridge 的 main thread 與 runner thread 共享真正
的 Handle；跨 thread 複合修改使用 `with handle.edit()`。

一個 Handle 仍只允許一條 runner thread，且只能跑一個 Round。Pi session 的 fork／resume
不代表 agentloop Handle 可以 fork／重用；兩邊 lifecycle 必須分開標示。

## Extension API 的可行性與限制

Pi extension 足以成為這套入口的「前端」。官方 API 明確允許 extension：

- 用 `pi.registerTool()` 註冊 Pi 模型可呼叫的 custom tool；
- 用 `pi.registerCommand()` 註冊人可直接輸入的 slash command；
- 使用完整互動式 `ctx.ui` 顯示通知、確認、輸入或自訂元件；
- 用 `session_start`／`session_shutdown` 等 lifecycle events 建立及清理 extension state；
- 用 `pi.appendEntry()` 保存可重建的 extension metadata；
- 在 extension 中使用 Node.js built-ins 與 npm dependencies，因此可自行使用
  `node:child_process` 管理 Python subprocess；tool/handler 也能接收 abort signal。

這代表 tool、command、UI 和 Pi session lifecycle 都不需要另造。但 extension API **沒有把
Python object 變成 JavaScript object 的能力**，也沒有替我們提供 agentloop-aware 的 subprocess
supervisor。長期 Python child、JSONL correlation、stderr、crash/restart、timeout 與 orphan
cleanup 都仍是本 adapter 的責任。

另一個重要限制是 Pi session replacement：官方 lifecycle 在 `/new`、`/resume`、`/fork` 時會
對舊 extension instance 發 `session_shutdown`，接著重新載入 extension。若 Python Round 要比
Pi session 活得更久，就不能只把 child reference 放在 extension 記憶體；那時才需要獨立
service 與 reconnect protocol。第一版採同生命週期：extension shutdown 就嘗試 `end`／`join`
並關閉 child。

## 可行方案比較

| 方案 | 優點 | 問題 | 結論 |
|---|---|---|---|
| **Extension-only**：只寫 TypeScript extension，不設 bridge | 最少檔案；直接有 tools、commands、UI | 無法直接持有 Python Handle；每次 shelling out 都是新 process，不能可靠控制活著的 Round；嵌入 Python 又引入 ABI/runtime 複雜度 | 不可滿足核心需求 |
| **Extension + Python bridge**：extension 啟動 child process | 保留 Pi TUI；tool/command 共用一個 client；生命週期集中；Handle 留在 Python | 要定義 JSONL protocol；extension 自己負責長期 child lifecycle | **推薦第一版** |
| **純外部 launcher/adapter**：Python 啟動 `pi --mode rpc` | 不必寫 Pi extension；Python 可同時持有 Handle、管理 Pi RPC；官方 RPC 適合跨語言 | Pi 變成 headless；需重做 UI/事件呈現，控制方向也與「Pi 作入口」相反 | 適合 CI/自動化，不是第一個互動入口 |
| 獨立 Python service + Unix socket，再由 extension 連接 | 多個 Pi／client 可連接；service 可長壽、可跨 Pi session | authentication、discoverability、斷線重連與多 client 衝突立刻變成必要問題 | 後續多 client 再考慮 |
| Node.js 使用 Pi SDK，再連 Python | Pi state 型別完整、可深度客製 | 仍需 Python IPC；比 extension 多負責 session/UI | 日後自訂產品介面才考慮 |
| Pi `bash`／tmux 操作 Python REPL | 幾乎零 adapter | 輸出解析脆弱、難做 correlation、無可靠 callback boundary、容易留下孤兒 process | 只適合手動實驗 |

結論不是「只做一個 extension」或「只做一個外部 adapter」，而是把兩者各自限制在適合的
責任：**Pi extension 負責互動入口，Python bridge adapter 負責真正的 Handle/controller。**
兩者用一條小型 JSONL protocol 相接，agentloop 核心保持不變。

## 推薦的最小入口

最小入口由兩個薄元件組成：

1. `pi-agentloop.ts`：project-local Pi extension，註冊一個 `agentloop_control` custom tool，
   以及少量給人的 slash commands；負責啟動及監看 bridge child process。
2. `pi_bridge.py`：Python controller，透過使用者提供的 factory 建立 `bot`、`dispatch`、
   `Handle(auto_finish=False)`，再用 `agentloop.threading.start()` 啟動 runner。

`agentloop_control` 第一版可以只有這些 action：

- `start`：由具名 factory 建立單一 Round；不要接受任意 pickle 或任意 Python expression。
- `status`：取得精簡 snapshot。
- `wait`：等待下一個 state／boundary event，設 timeout，避免 Pi tool call 永久卡住。
- `pause`、`resume`、`end`：映射到 Handle 同名控制桿。
- `edit`：套用明確定義的 JSON patch operations；先支援 `prompt`、`images`、
  `ask_options`、`tool_calls`、`tool_results`、`auto_finish` 等可序列化欄位。

人用 slash commands（例如 `/al-status`、`/al-pause`、`/al-resume`、`/al-end`）與模型用
custom tool 應呼叫同一個 extension client，不維護兩套語意。初版只管理一個 active Round；
等實際需要出現，再增加 Round registry。

`dispatch` 內的 Python functions、`bot`、`reply` 等 process-local objects 必須由 Python
factory 建立。Pi 只能修改 bridge 明確暴露的 JSON representation；「Handle 全公開」是
Python 同 process API 的自由度，不等於跨 process 可以無損搬運所有物件。

## 資料與控制流

一般觀察流程：

```text
人 → Pi prompt
  → Pi 呼叫 agentloop_control(status/wait/...)
  → extension 寫一行 JSON command 給 bridge
  → bridge main thread 操作或讀取 Handle
  → bridge 寫一行帶 request id 的 JSON response
  → extension 回傳精簡 tool result 給 Pi
  → Pi 解釋狀態或決定下一個 action
```

agentloop 邊界事件：

```text
runner: ask() → commit Step → after_step callback
                                   │ snapshot/event
                                   ▼
                              bridge event queue
runner: tools → commit results → after_tools callback
                                      │ snapshot/event
                                      ▼
                                 bridge event queue
```

Bridge protocol 應使用 request id 對應 response，event 則有單調遞增 sequence number、Round
id、boundary type、`state` 與 `step`。stdout 只放 protocol JSONL，診斷一律寫 stderr，避免
破壞 framing。大型 message/tool result 必須截斷並提供「已截斷」metadata；完整資料可由明確
的 detail action 分頁取得。

建議 snapshot 僅包含可序列化報告：`state`、`step`、`message`、`tool_calls`、
`tool_results`、`tool_log` 摘要、token counters、`stop`、`end_reason`、`err` 的安全文字形式。

## pause、resume、end 與 callbacks 的映射

| Pi action | Python 行為 | 邊界語意 |
|---|---|---|
| `pause` | `handle.pause()` | 立即接受要求，但等 Step+`after_step` 或 tool batch+`after_tools` 完成才停 |
| `resume` | 必要修改包在 `handle.edit()`，再 `handle.resume()` | 只喚醒 `waiting`／`paused` 或取消尚未生效的 pause |
| `end` | `handle.end(reason="pi")` | parked 時立即完成；running 時等下一個安全邊界，不強殺 operation |
| `status` | 在 lock 下製作 JSON snapshot | 只觀察，不改控制流 |
| `edit` | `with handle.edit(): ...` | 修改本身不會自動 resume，也不代表 Round 未完成 |

Callbacks 仍在 Python runner thread、持有 Handle 的 `RLock` 同步執行。Pi extension 不可能
成為真正的同步 callback：它在另一個 process，事件送達時 runner 可能早已進入下一個
operation。因此 callback 只能做兩類事：

- **observe mode**：callback 把 snapshot 放進 Python queue，回 `CONTINUE`；Pi 看到的是
  事後通知，不能保證趕在 tools 或下一個 Step 前修改。
- **gate mode**：callback 先把 snapshot 放入 queue，再回 `PAUSE`；runner 安全停住後，Pi
  才能可靠檢查／修改並 `resume()` 或 `end()`。

推薦最小版支援可配置的 gate：`after_step`、`after_tools` 各自可設 `continue` 或 `pause`。
若要批准或改寫 tool calls，必須讓 `after_step` gate 回 `PAUSE`；若要改寫 tool results，
必須讓 `after_tools` gate 回 `PAUSE`。不要讓 callback 等待 Pi response，也不要在 callback
裡啟動 child thread 後 `join()`；callback 應只 enqueue immutable snapshot 並迅速返回。

Agentloop callback 自己回 `END` 與 Pi 呼叫 `end` 最終都進入 Handle 的完成狀態，但發起者
不同。Bridge 應保留 `stop`／`end_reason`，讓 Pi 不要把 Limits、callback policy 與人工結束
混為一談。

## 已實作的 scripts 與 example

```text
repl/
├── docs/
│   └── pi-coding-agent.md
├── scripts/
│   ├── pi-agentloop.ts       # Pi extension：tool、commands、child lifecycle、JSONL client
│   ├── pi_bridge.py          # Python bridge：factory、Handle、runner、callback events
│   └── check_pi_bridge.py    # 離線 subprocess protocol smoke test
└── examples/
    ├── pi_minimal_factory.py # 離線 bot/dispatch factory
    └── pi_quickstart.md      # 啟動、commands、factory contract 與實例
```

Bridge 預設 `after_step=pause`、`after_tools=continue`；`start` 可透過 `gates`
覆寫。Factory 只能由 bridge 啟動參數或 `AGENTLOOP_PI_FACTORY` 指定，不接受
Pi tool payload 動態指定 import path。完整用法見 [Pi quickstart](../examples/pi_quickstart.md)。

## 風險

- **雙 agent 競爭**：Pi 與 agentloop 都可能修改同一工作樹；必須清楚指定誰能執行哪些工具，
  否則會有競態與難以歸因的變更。
- **循環與成本**：Pi 模型控制另一個模型迴圈，可能形成「status → 再問 → 再 status」循環；
  需要 Pi 側最大 actions、timeout，以及 agentloop 側 Limits。
- **權限**：Pi extensions 與 Python tools 都是任意程式碼，不是 sandbox。Factory path、edit
  欄位及 bridge 啟動參數不可當成不可信輸入直接執行。
- **生命週期**：Pi reload、session shutdown、terminal crash 可能留下 bridge 或 parked runner。
  正常 teardown 應先 `end()`、再有限時 `join()`；強制殺 process 只能列為異常收尾。
- **protocol 污染**：bridge stdout 的普通 print 會破壞 JSONL。bot/tool 日誌必須導向 stderr
  或檔案。
- **序列化落差**：公開 Handle 包含 Python objects；remote snapshot 只能是有意義的子集。
- **事件背壓**：Pi 忙於自己的 model/tool turn 時仍可能累積 boundary events；queue 必須有
  上限、sequence number 與 dropped-event 記錄。
- **過時修改**：Pi 根據 step N 的 snapshot 發 edit 時，Round 可能已到 step N+1。修改命令
  應帶 expected state/step，bridge 不匹配就拒絕；gate mode 可大幅降低此風險。
- **錯誤混淆**：Pi tool error、bridge protocol error、agentloop `state == "error"` 是三層不同
  錯誤，結果格式必須分開表示。
- **版本漂移**：Pi 的 package scope、extension types 與 tool result schema需鎖版並測試。

## 待決問題

1. 第一版是否預設 `after_step=pause`、`after_tools=continue`，還是兩者都只 observe？若目標是
   讓 Pi 真正改 tool call，前者較能展示整合價值。
2. Python factory 的穩定介面是 `module:function`、設定檔，還是由 bridge script 本身當範例
   直接 import 應用程式？
3. `edit` 採 JSON Patch、固定 actions（例如 `replace_tool_call`），或兩者並存？固定 actions
   較安全易懂，JSON Patch 自由度較高。
4. Pi 模型是否預設能使用 `resume`／`end`，還是這兩項只給人類 slash command？這是 policy，
   不應硬編進 agentloop 核心。
5. Pi exit 時，bridge join timeout 到期後要留下 process、警告使用者，或強制終止？agentloop
   沒有 unsafe cancellation，因此無法同時保證立即退出與完成 operation。
6. 是否需要把完整 snapshots 寫成獨立 event log，以便 Pi session reload 後重新連接？第一版
   可先不支援重連。
7. 何時值得從單一 child process 升級成 Unix socket service？明確需求應是多 client、長期
   Round 或 Pi reload 後保留 Round，而不是預先抽象化。

## 官方資料

- [Pi coding agent README](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/README.md)：
  互動模式、內建工具、extensions、packages 與程式化入口總覽。
- [Extensions](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/extensions.md)：
  `registerTool`、`registerCommand`、events、UI、生命週期與安全說明。
- [SDK](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/sdk.md)：
  `createAgentSession`，以及 SDK 與 RPC 的選擇建議。
- [RPC mode](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/rpc.md)：
  JSONL framing、commands/responses/events、`prompt`、`steer`、`abort`、`get_state` 等協議。
- [Extension examples](https://github.com/earendil-works/pi/tree/main/packages/coding-agent/examples/extensions)：
  custom tools、commands、permission gates、subagent 與 process 整合的官方範例。
