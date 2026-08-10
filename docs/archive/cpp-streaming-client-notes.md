# C++ HTTP／SSE client 草稿

> 歷史材料，最後內容日期為 2026-08-05。這份筆記沒有對應到本 repository 的目前實作，
> 也不是需求或 roadmap；保留它只為了讓網路、串流與生命週期的踩坑可被搜尋。

## 1. 網路與 SSE 解析坑點

- **TCP 分段與 line buffering**：libcurl 每次回傳的片段不保證以 `\n` 結尾。需自行
  緩衝，拼成完整行後再交給 JSON parser。
- **HTTP 狀態碼分流**：429／5xx 等非成功回應通常是一般 JSON，不是 SSE；應先判定
  status code，再選解析路徑。
- **指數退避**：對 429／5xx 設定有上限的重試次數與指數退避。

## 2. 串流與模型能力

- **並行 tool calls**：argument delta 可能依 `tool_calls[i].index` 交錯到達；需以 index
  分組組回完整 JSON。
- **Reasoning**：不同後端可能使用 `<think>` 標籤或 `delta.reasoning_content`；輸出時應
  與 final answer 分流。
- **中止串流**：async controller 可透過 libcurl progress callback 回傳非零值中止傳輸。
- **Usage 與除錯**：收取 SSE 結尾的 usage，並保留 raw-chunk hook 供診斷。

## 3. C++ 生命週期與 concurrency

- 背景 thread 若比呼叫端活得久，handler／controller 不可持有懸空 reference；需要明確
  的 shared ownership 或其他可證明的生命週期策略。
- 非同步封裝不可阻塞 UI 或 event loop；同時要明確定義取消後哪些副作用已發生。

## 4. 可選的上層封裝

- `ChatSession`：維護 system／user／assistant／tool messages。
- Vision helper：把本地圖片編碼為 data URI。
