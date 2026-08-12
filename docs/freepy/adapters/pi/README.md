# 以 Pi coding agent 操作 agentloop

本文記錄把 [Pi coding agent](https://github.com/earendil-works/pi/tree/main/packages/coding-agent)
當作 agentloop 進階互動入口的設計與第一版實作。
官方 API 與 package 名稱最後核對日期為 2026-08-11。

> **實作狀態：**Pi 0.84.1 已驗證能載入 TypeScript extension 與註冊 slash
> commands；Python bridge 的完整 gate、edit、resume、end、join 流程已通過離線
> subprocess smoke test。尚未使用真實 LLM factory 做端到端測試。

## 文件導覽

- [架構與 bridge 邊界](architecture.md)：process／thread 邊界、Pi extension 能力、方案比較與最小入口。
- [Protocol 與控制語意](protocol.md)：資料流、callback gate、pause／resume／end 與現有實作。
- [風險、待決問題與來源](considerations.md)：部署風險、尚待決策事項與官方資料。
- [Quickstart](quickstart.md)：離線驗證、載入 extension、factory 與操作命令。

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
