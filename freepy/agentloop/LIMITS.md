# Limits 的能力邊界

`agentloop.limits.Limits` 是 agentloop 隨附的簡單 callback policy。它處理的是 agentloop
已經知道、能在 Step 邊界直接判斷的資料，不追求完整的執行環境隔離。

使用方式、欄位與自訂控制流範例見 [`agentloop.limits` 的獨立文件](limits/README.md)。

## 內建 Limits 負責什麼

- Step 數量；
- 工具總呼叫次數；
- 單一工具呼叫次數；
- 工具白名單；
- 模型白名單；
- Round 經過時間；
- 模型回報的 input 與 output token 用量。

這些限制都透過公開的 `after_step` callback 執行。Limits 沒有隱藏入口，核心
`agentloop.run()` 也沒有專門為它保留分支。

## 合作式限制

Limits 只在一個 Step 完整提交後檢查，因此它不會：

- 切斷正在進行的模型請求；
- 切斷已開始的工具批次；
- 回滾工具已經造成的副作用。

時間或 token 上限可能超過最後一個 operation 的少量用量。input 與 output
分開限制；cached input 另行公開給外部計價 policy，但仍包含在 input 用量內。
這是合作式邊界的正常結果，
不是硬性資源配額。

## 明確不負責

內建 Limits 不處理：

- CPU；
- 記憶體；
- GPU；
- 網路；
- 檔案系統隔離；
- subprocess 或 Python process 的強制終止。

agentloop 位於同一個 Python process 裡，沒有能力可靠地提供這些隔離。需要時應由呼叫者
在 process、作業系統、container 或 VM 層處理；本子專案不假裝提供，也不因此擴大核心。

## 使用者可以換掉它

Limits 只是內建預設工具，不是 policy framework。使用者可以：

- 直接使用內建 Limits；
- 在它旁邊組合自己的 callbacks；
- 基於 `Limits.after_step()` 擴充；
- 完全不使用它，自行用 `after_step`、`after_tools` 與 `CONTINUE/PAUSE/END` 建立控制流。

內建版本保持簡單；更精細的安全、計費或組織 policy 留給外部控制者。
