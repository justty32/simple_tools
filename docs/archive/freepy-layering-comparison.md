# 分層定案與既有文件對照

> **歷史稽核紀錄。** 當前定案直接看
> [`docs/freepy/architecture.md`](../freepy/architecture.md)；本頁只保留當時發現的混層問題。

本頁比較 [`architecture.md`](../freepy/architecture.md) 的「core → Controller → shell → libraries →
外圍服務」模型與現有文件；不是新功能規格。結論以程式的已實作邊界優先。

## 大致一致

| 你的分層 | 已有依據 | 判斷 |
| --- | --- | --- |
| agentloop 是原始本地核心 | `agentloop/README.md`、`ROUNDS.md` | 一致；`run`、Handle、callback、safe boundary 不需要 service。 |
| Handle 保留直接能力 | `ROUNDS.md` | 一致；它刻意 mutable，進階使用者自行承擔風險。 |
| Pi／REPL 是操作入口 | `agentloop/repl/README.md`、Pi 文件、`shells/README.md` | 一致；Pi 不擁有 Handle，也不接管 Pi 自己的 loop。 |
| tooljson／llmkit／base_tools 是下層能力 | 三者 README | 一致；`llms` 只做一次 request，tooljson 只產 schema+dispatch，tool 才做外部 effect。 |
| persistence／scheduler 是外圍系統 | `CONTROL-PLANE.md`、`ADAPTERS.md` | 一致；它們處理大量 dormant bot、lease、恢復與公平性，不是單 Round 的基本需求。 |

尤其 `ADAPTERS.md` 已明講：先完成 agentloop、收斂 Python operations／REPL，有多 bot 的實證後
才抽 scheduler/service。這非常接近本定案。

## 已校正與仍待處理的混層

1. `agentloop/CONTROLLER.md` 與 `controller.py` 已改為本地 wrapper：保留 `.handle`，提供逐步、
   前景與單一背景 runner；不再公開 Task、revision、Candidate 或 claim。
2. `ROADMAP.md` M1 說 runner 產生 durable commands，`agent_machine/PLAN.md` 也把同一組詞接到
   adapter。正確方向是：`advance()` 可提供逐步 primitive；durable command 由外圍 adapter
   轉譯／保存，不能成為 Controller 的本體。
3. `docs/roadmap-guide/agentloop.html`、首頁與 Agent Machine 導讀已按新分層修正：Candidate、
   SQLite、scheduler 是後續服務，非 Controller。
4. `agent_machine` 的純資料 M0 code 已開始建立，但不應被描述為 Controller 的下一版；它是
   另一個可選外圍 kernel／service，應暫停在明確的 adapter 邊界前。

## 你的模型還需要補的四個界線

### 1. Controller 不是 security boundary

Controller 的定位是「C with class」式的第一個上層抽象，不必宣稱特別安全。它可以把常用操作、
預設與 runner 樣板做得舒服，也可以逐漸演進；但 Handle 必須保留，其他人也可在其上建立不同
抽象。要避免的只是把 OS sandbox、ACL 或 durable scheduler 誤說成 Controller 的保證。

### 2. shell 有廣義與 Unix 原義

廣義 shell 是使用者可直接互動的介面，所以 REPL、GUI、vim 式介面、Pi extension 都可算；Unix
原義的 kernel 薄殼則更接近 `shells/` launcher 與未來的 Python library + REPL／CLI。Pi bridge／MCP
因為要處理 JSONL、timeout、child cleanup 與跨 process lifecycle，較精確是 control adapter；
它們仍不應擁有 loop。

### 3. libraries 是基礎能力，但還要補資源與隔離

tooljson 是描述／載入／dispatch library；base_tools 和 exec tool 才會真的碰檔案與 process。
把它們理解為未來 Agent Machine 的 `/usr/lib` 是合適的，但 endpoint、工具和 context 還有金錢、
可用度、延遲與不確定性，外層需有資源管理。root check、approver 與輸出上限只是便利 policy，
不是 sandbox；真正隔離仍是未來 runtime 的獨立 process、OS permission／namespace 等責任。

### 4. 外圍服務的資料邊界（暫緩決定）

若未來真的要 persistence，service 不應存整個 Handle、callback 或 Python callable；比較合理的
方向是只存可序列化的 intent、已提交結果與 effect 狀態，再由 runtime adapter 建立本地
Controller／Handle。但資料模型與責任分配現在暫緩，等真正需要重啟或多 process 協調時再定。

## 建議的下一個決策

Controller 的本地 API 已完成並有離線關卡。下一個決策不再是擴寫 Controller，而是等真的出現
跨重啟或多 process 的使用情境後，再為外圍 Agent Machine 決定可序列化資料與 adapter 邊界。
