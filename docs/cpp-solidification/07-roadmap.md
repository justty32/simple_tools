# 分期路線、工期級距與驗收

工期以一位熟悉現有語意、C++23、CMake 與 Linux 系統程式的工程師粗估；是 person-week 級距，不是交期承諾。需求澄清、跨平台、dependency 選型與 sanitizer 修復都可能擴大範圍。

## Phase 0：contract／differential harness（1–2 週）

- JSON canonicalization、error normalization、fixture runner。
- Python smoke 轉成 machine-readable vectors/trace。
- CMake clean build；Linux GCC/Clang + Windows GCC matrix。
- sanitizer/fuzz 最小 pipeline。

Gate：差異能定位 field／transition；不能只印兩坨 JSON。

## Phase 1：`tooljson` pure／exec core（2–3 週）

- parser、strict validator、registry、argv/stdin decision、clip。
- Python type 明確回 external intent，不嵌 interpreter。
- Linux process adapter 後接；pure vectors 先跨平台。

Gate：pure vectors 100% 等價；Linux 45 類現有 case 全過；unknown type/version fail closed；parser fuzz 無 crash。

## Phase 2：modelcards + identity/common values（1–2 週）

- card corpus validation／lookup／projection。
- `AgentPath`、ids、permission/resource values。
- 先整理 proxy/modelcard verified source-of-truth。

Gate：31 類 card 行為等價；path property、subset、conservation laws 全過。

到這裡形成最小有價值 native core，合計約 **4–7 person-weeks**。若此時 dependency/build/debug 成本已大於部署收益，可在這個安全點停。

## Phase 3：agentloop semantic core（3–5 週）

- Turn/Round reducer、pending debt、limits、quiet、finish/usage。
- control event queue、completion/enqueue、tool input/cancel。
- fake Bot／Tool adapter replay；不接真模型。

Gate：現有 35 情境等價，補 race/cancel/timeout/retry；TSan 或等價檢查無 data race。

## Phase 4：LLM protocol與 transport（2–4 週）

- message/tool-call/SSE normalized events。
- 先 Python transport，選配 native local-proxy client。
- malformed fragment、disconnect、usage-only chunk、retry fixtures。

Gate：fake OpenAI server 全離線；真 provider 只作非阻塞 smoke。沒有證據時不移除 Python adapter。

## Phase 5：filesystem/process/supervisor（3–6 週）

- base file/edit/path adapter。
- POSIX exec、pipe、timeout、partial output。
- policy template、runtime fake backend；之後才接 container/cgroup。

Gate：fault injection、process tree cleanup、child 不升權；Windows unsupported case明確 fail closed。

目前已實作部分做到混合 native default，粗估 **10–18 person-weeks**，另預留約 25% stabilization／CI／文件。若要求 native TLS/SSE、Windows process parity、CPython embedding 或直接移除 LiteLLM，範圍會顯著增加。

## Phase 6：規劃中多 agent 系統（6–12+ person-months）

這不是「移植」，而是新產品實作：

1. communication + authoritative storage。
2. memory + context manifest + introspection。
3. runtime ledger/supervisor + team saga。
4. agentfs provider，再選 FUSE/9P adapter。

須等各 Python prototype／contract 穩定後逐 slice 估。直接用 PLAN 頁數估 C++ 工期沒有意義。

## 建議的第一個 PR

只做：

1. `fixtures/tooljson/` 15–20 個 pure vectors，含 canonical expected/error。
2. 最小 CMake C++ library + CLI；strict envelope parser、exec argv builder，不跑 process。
3. Python/C++ differential runner，輸出 JSON diff。
4. Linux/Windows clean build，parser fuzz seed corpus。

不要在第一 PR 加 HTTP framework、plugin DLL、CPython、FUSE、agent runtime 或 package manager 大整合。這個 PR 的目的，是驗證 contract 與 native engineering loop。

## Benchmark 應量什麼

- cold start、idle RSS、一次 parse/validate、10K reducer events。
- 100 agents 的 state footprint與 control-event throughput。
- build time、binary/dependency size、crash symbolization。
- 真工作端到端另列 LLM/tool wait，不用它掩蓋 core regression。

預期 C++ 能改善 startup、RSS、嵌入與高頻 event處理；**不預期顯著縮短模型推理時間**。如果實際 workload 幾乎全在 LLM與外部工具，性能不是遷移主因。

## 暫停／退出條件

- protocol 仍頻繁變，differential mismatch 主要是規格空白。
- native dependency 或 ABI 比 Python deployment 更脆。
- sanitizer/race 問題無法維持綠。
- 第二實作沒有實際 consumer，只為語言偏好存在。
- benchmark 沒有部署／資源收益，卻要求永久雙維護。
