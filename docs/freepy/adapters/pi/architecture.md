# Pi adapter 架構與 bridge

[← 返回 Pi adapter 總覽](README.md)

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
