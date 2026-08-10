# 風險、反例與非目標

## 1. Framework gravity

大框架會誘使所有狀態塞回同一個 Agent/Crew/Graph object。freepy 反而需要 owner 清楚：agentloop 擁有 Turn，team 擁有 task/grant，runtime 擁有 instance/effective policy，memory 擁有 object/ref，workflow 擁有 checkpoint，agentfs 只 projection。

## 2. Hidden model calls

Instructor re-ask、guardrail repair、Crew manager/delegation、LightAgent planning 都可能在一次高階 API 內多叫模型。若不攤開成 Round/attempt event，usage、budget、context manifest 與 replay 都會說謊。

## 3. Retry is not recovery

重新呼叫純 parser 很安全；重新付款、寄信、寫檔、spawn 則可能重複副作用。retry policy 至少區分：

- pure/retriable；
- read with stale-result risk；
- idempotent effect with provider key；
- non-idempotent effect，需 approval/reconciliation。

## 4. Checkpoint is not exactly-once

LangGraph 能去重自己的 pending writes，CrewAI Flow 能保存 state，但外部世界不參與同一 transaction。需要 outbox、stable idempotency key、receipt、status reconciliation；無法確認時標記 `unknown_effect`，不能假裝 rollback。

## 5. Routing is not authority

Swarm 回傳 Agent、Crew manager delegation、LLM router 都只能產生 intent。target 名稱/path、role/backstory、graph edge 不會自動創造 grant、mount、network 或 budget。

## 6. Mutable shared state

MiniChain global context、PocketFlow shared dict、LightAgent mutable instance、Crew threads 都會在並行/巢狀/重入時出問題。durable state 使用 immutable input、versioned update、explicit reducer；ephemeral cache 必須標明 run scope。

## 7. Memory injection

retrieve 到的文字可能過期、低可信或含 prompt injection。source/provenance/trust/scope/expiry 應是資料欄位；載入 context 時放在 untrusted data block，不能因內容像 system/user 指令就提升 authority。

## 8. Guardrail is not sandbox

regex、schema、hook、approval、workflow edge 都不能阻止 process 繞過它們。真正限制檔案、network、CPU/memory、process tree 仍靠 runtime/container/cgroup/seccomp/mount。這些框架本身不是 container-based isolation 技術。

## 9. Telemetry 與商業層

CrewAI AMP、LangSmith deployment/observability 是外部/商業控制面，不是 OSS library 自動具備的能力。任何 telemetry 預設 local/off；啟用前審查 endpoint、payload、secret redaction、tenant/retention。

## 10. Copying vs learning

六個專案為 MIT，LightAgent 為 Apache-2.0。借「觀念」通常不構成直接複製；若移植 source、tests 或 docs，仍要保留相應 license/notice 並做 dependency/SBOM 審查。

## 非目標

- 不做星數排行或宣稱某框架全面勝出；
- 不因 quickstart 短就推論 production 成本低；
- 不在這次調查中替換 freepy runtime；
- 不把 graph node、model Round、agent Turn、team task、world tick 合成一層；
- 不用 agentfs 的 path 字串直接打開 host filesystem；
- 不先支援所有 provider、DSL、stream mode 或 remote control plane。

## 最終反證問題

每次想搬一項功能前問：

1. 它增加了新語意，還是只增加一個漂亮 API？
2. owner、identity、authority、durability、effect boundary 分別在哪？
3. crash/retry/resume 後，證據能否說清楚「已做、未做、未知」？
4. 拿掉框架名稱後，是否能用小型 versioned protocol 表達？

四題答不清楚，就先不要固化進核心。
