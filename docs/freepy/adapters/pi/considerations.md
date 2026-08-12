# Pi adapter 的風險與決策

[← 返回 Pi adapter 總覽](README.md)

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
