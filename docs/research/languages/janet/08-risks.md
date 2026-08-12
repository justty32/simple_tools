# 風險、反證與待決策

## 已知風險

| 風險 | 目前證據 | 對策 |
|---|---|---|
| 把可表達誤認為可部署 | FUSE/9P、cgroup 未在 Janet repo 實作 | core/backend 分層，逐項 gate |
| Windows process 不穩 | 3 個 pi-shell tests access violation | Linux-first；Windows fail closed |
| HTTP 能力不足 | `spork/http` 無 TLS/SSE | local LiteLLM 或 Python transport |
| Python reflection 鎖死格式 | tool schema 來自 signature/docstring | schema 先落 JSON，再由兩語言讀 |
| 兩份實作漂移 | 尚無 differential harness | shared fixtures + normalized trace |
| Lisp 隱喻過度延伸 | path 容易被當成 authority/command | typed handle、ACL、operation schema |
| `marshal` 成為隱藏 ABI | 可存 closure/fiber 但不跨語言 | 只作暫存；持久層用版本化資料 |
| FFI 擴大 TCB | ownership 錯可 segfault | 優先 subprocess/local HTTP |
| build 交付舊程式 | jpm import dependency 追蹤不足 | CI clean build + artifact hash |

## 最重要的反證

若只看語言功能，會說 Janet 已有 HTTP、process、fiber、FFI，因此全部可移。但本次測試同時看到：HTTP 無 TLS/SSE、Windows subprocess crash、FUSE/9P 無任何 repo 證明。正確結論是「語意核心可行，部分 effect backend 未證明」，不是「全系統已可 Janet 化」。

## 安全風險

- Janet core 產生 decision，不應直接持有 Podman socket 或 host root。
- root containment 只限制 file tool 的 path，不限制 shell、network、PID、memory。
- memory link 不是授權；list/search 也不得洩漏 forbidden object 的名稱。
- context 中的 tool/peer/web 內容永遠是 data，不能升成 system authority。
- child policy 必須由父 effective policy 推導，而非父／模型自報 request。
- raw argv、mount、network destination 必須在 supervisor 端重建與驗證。

## 語意待決策

### 1. Janet 的部署角色

可選：嵌入 Python host、獨立 stdio service、或成為主 agent process。建議先 stdio CLI；最容易 diff、隔離 crash、版本化 protocol，也不綁 Python ABI。

### 2. tool body type

是否新增 `_type:"janet"`？建議先不要急。portable tool 用 `_type:"exec"`；Janet-native fast path 等第二個實例出現再定，避免過早鎖格式。

### 3. streaming

短期是否必須保留 token streaming？若是，Python transport 為必要 adapter；若 agentloop 只需完整 message，Janet local HTTP 可先上線。

### 4. Windows 支援等級

建議明定：pure core 支援 Windows；runtime/process production 先只支援 Linux。若要 Windows production，需獨立修復与 process/argv/pipe 測試矩陣。

### 5. authoritative store

file mailbox 適合第一版；team、budget、task 若進到多 process crash recovery，可能需要 SQLite transaction。介面保持 storage-neutral，不必為「Unix 味」拒絕資料庫。

## 決策記錄建議

每個固化 slice 留一份短 ADR：

- Python demo 觀察到什麼。
- 哪些行為被凍結，哪些明確 undefined。
- Janet 接管哪些 invariant。
- adapter 仍擁有哪些 side effect。
- differential fixtures 在哪。
- fallback 與移除條件。

這會讓「Python demo → Lisp 固化」成為可審核工程流程，而不是依個人偏好的 rewrite。

## 最終判斷

值得做，但應以 `tooljson` 小切片驗證方法，再逐步擴到 identity、agentloop 與 memory。
preset 只是設定資料，不列為固化階段。真正隔離繼續交給 container/kernel；Janet 負責讓
上層語意小、清楚、可重播、可組合。
