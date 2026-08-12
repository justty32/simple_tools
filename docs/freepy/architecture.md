# FreePy 分層定案

這份記錄目前的架構判斷；它用來校正 ROADMAP 與局部規格中的詞彙，不取代各 package 的
公開 API 契約。新規劃文件仍維持 8 KB 以下。

## 由內向外

```text
agentloop                         原始本地核心（像 assembly，或極簡 C）
  loop + Handle + Limits

Controller                        第一個較有結構的本地封裝（像 C，或初版 C++）
  組合 Handle；提供較窄、較容易正確使用的控制方式

互動入口                           廣義的 shell
  REPL、GUI、vim 式介面、Pi extension、MCP 等
  人或另一個 coding agent 操作 Controller／Handle 的介面

llmkit / tooljson / tools         系統函式庫與 adapters（像 /usr/lib）
  endpoint、訊息／tool 格式、read/write、shell、socket 等能力

durable application/control plane 外圍服務
  persistence、event store、scheduler、candidate、goal/evidence/verifier、
  cross-process worker、resource accounting、team 與 projection
```

## 各層責任

### agentloop

`Handle` 是故意直接、可變、同 process 的把手；`run()`、safe boundary、callbacks 與
`Limits` 是最小執行語意。它不需要 database、queue、lease、round id 或跨 process RPC。

### Controller

Controller 不取代 Handle。它是第一個「C with class」式的本地抽象：可把常見操作、狀態觀察、
預設 policy 與 runner 樣板收在一起，讓一般使用者少寫樣板、較容易用；但它不是 security
boundary，也不壟斷 Handle 上能長出的其他封裝。進階使用者可直接使用 Handle，或自行建立另一種
上層 API。Controller 也會隨實際使用持續演進。

第一版 Controller 不應被 durable command、revision、SQLite、scheduler、Candidate、goal completion
或 worker lease 定義；這些若需要，屬於更外層服務的協定。

### Shell 與 libraries

「shell」在這裡取廣義的「使用者可直接互動的介面」：除了 bash/fish/zsh，也可以是 REPL、GUI、
vim 式介面或 coding-agent extension。若取 Unix 原義的「kernel 薄殼」，最接近的是未來的 Python
library + REPL／CLI；Pi bridge、MCP 等則較準確叫 *control adapter*，因為它們需處理跨 process
protocol、timeout 與 lifecycle。

這些入口都不擁有 agentloop 的真實狀態，而是以明確 protocol 操作本地 Controller／Handle。
llmkit、tooljson、base_tools、exec_tools、http_tools 等 packages 是未來 Agent Machine 的基礎函式庫：提供 endpoint、tool format、
檔案／process／網路等能力。它們和 `/usr/lib` 的相似處是供上層重用；差異在於 endpoint 的成本、
可用度、延遲與不確定性要由外層資源管理。工具副作用、權限與 sandbox 則仍由 tool/runtime adapter
負責，不能因 library 類比而被省略。

### 外圍服務

只有需要重啟恢復、多 process／多 machine 協調、排程、資源帳本、可驗證完成或審計時，才在
Controller 外建立 durable application/control plane。它可把 Controller 當一個 runtime adapter，
但不能回頭把其資料庫術語污染 Handle 的日常 API。

## 目前修正

此前把 `AskStep`、`RunToolBatch`、`Candidate`、revision 與 in-memory queue 直接放進
`agentloop.Controller` 的嘗試，已移除；它們屬於未來外圍服務的協定。`advance()` 保留為 loop
的低階逐步 primitive，但不是 durable command API。

Controller 現在已有最小本地 API；後續先從實際使用回饋演進，不急著接 SQLite 或 scheduler。
