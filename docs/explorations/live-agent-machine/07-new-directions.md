# 十二個新方向

以下不是 roadmap 承諾，而是從「world 是 function、Linux 是 editor、Agent Machine 管 assurance
經濟」推導出的產品與研究假說。

## 1. Workspace Form Compiler

唯讀掃描 workspace、tool specs、測試、文件、policy 與 goal，產生 WFIR 候選：entry、reads、
writes、mounts、exports、postconditions 與 unknowns。沒有 schema 的內容保持 data/unknown，絕不
自動升成 command。它是 future editor 的 parser/type checker。

## 2. Evaluation Browser

以 `Step manifest → proposal → intent → effect → delta → evidence → decision` 為主軸瀏覽 Round，
並能回到 raw file/event。它會是 agent loop 真正的 debugger；transcript 只是其中一個 view。

## 3. Assurance Broker

對 artifact 或 claim 附 proof-carrying envelope：workspace generation、test hash、tool/schema version、
verifier、independence tags、human acceptance 與 expiry。consumer 不問「哪個 agent 說的」，而問
「我要求的 postcondition 是否有仍有效的 evidence」。

## 4. Human Stability Router

從 assurance graph 選擇少量最有價值的人工 cut points：能解除最多下游阻塞、影響不可逆
effect、存在高相關錯誤或需要意圖裁決者優先。輸出 typed decision card，而不是轉寄整串聊天。

## 5. Assurance Garbage Collector

像 memory GC 一樣維護 evidence graph：上游 generation 改變或 grounding 撤回時，精確標記 stale
decisions，排定重驗，清掉只有 cache 價值的 derived view。它回收的不是「真理」，而是已失效
的可用保證，避免 certainty cache 永遠綠燈。

## 6. Counterfactual Workspace Forks

固定同一 workspace/context manifest，分支替換 engine、context LOD、tool policy、patch 或
verifier；比較成本、效果與 evidence。這能回答失敗來自模型、context、工具、規格還是世界
變動，比在同一 chat 說「再試一次」更可診斷。

## 7. Context Pager Inspector

顯示每個 block 為何 inline/card/cold、token 成本、source、trust 與上次載入時間；人可 pin、
降低 LOD 或檢查 provenance。context management 從隱藏 prompt 魔法變成 image editor 的記憶體
debugger。

## 8. Plan Shell 2：Intent Graph

保留原型的 `inspect / plan / ask_user` 三出口，但輸出不再是立即可跑的 shell，而是帶
precondition、scope、rollback/effect 類型與 verifier 的 intent graph。shell 只是 supervisor
驗證後可選的 materialization backend，直接補上既有 TOCTOU 教訓。

## 9. Tool Foundry／Spec Crystallizer

把 trace、counterexample、fixture、tooljson schema、fake adapter、fuzz corpus、Janet differential
vectors 與 dcap native project 包成同一條生產線。LLM 產生的是候選行為；真正能累積的產品是
contract 與第二個獨立 implementation。

## 10. Evidence-native Git

把 commit/tree、Agent Machine generation、Round、prompt/context manifest、tool effects、CI 與
human decision 接成 provenance graph。`git blame` 升級成 evaluation blame：不只問誰改一行，
還問它根據什麼世界、花多少成本、經何驗證、是否仍在 acceptance scope 內。

## 11. Capability／Namespace Debugger

視覺化某 agent 此刻的 `/work`、`/tools`、`/memory`、`/srv`，比較 declared、effective、observed，
並顯示 handle 綁定與 revocation generation。這會直接抓出「設定寫已隔離，但 backend 根本沒做」
或「知道 path 就以為有權限」的錯覺。

## 12. World-tick Workbench

對多 agent 不以某個 chat completion 為完成，而以 world transition 為主：收集各 agent 的
Round/report/artifact、在 barrier 或 deadline 形成 position snapshot，顯示 assurance 缺口後才
commit 下一個 tick。父 agent 只讀交付與 evidence，不偷看所有 child private context。

## 更遠的推論

### Program package 會變成「可掛載 closure」

未來分享 agent program 可能不是傳一個 prompt template，而是傳：immutable workspace root、
form manifest、所需 capability interfaces、goal schema、fixtures、memory refs 與 assurance policy。
receiver 將它 mount 到自己的 namespace，以自己的有效權限重新 type-check 後求值。

### 人類介入會像 exception handling

人不再常駐看每一步，而是在 machine 無法以既有 evidence/policy決定時收到 condition；介面列出
合法 restarts、各自成本與影響。這比 approval popup 更接近真正的人機合作：人處理語意斷點，
machine 處理可機械化的葉節點。

### Interactibility 會形成 cognitive paging

人類也有有限 context。Assurance aggregator 應替人編譯 review manifest：目前決策、最小 evidence
slice、反例、下游影響與可選 restart；其餘以 link/card 延後載入。換句話說，context compiler
不只服務模型，也服務 reviewer。

### 系統會累積「確定性債務」

若為趕時間接受 probabilistic claim、stale evidence 或未解 condition，machine 應記成 explicit
assurance debt，附 scope、風險與到期日。後續排程可在低壓時補測試／重驗；不能讓暫時 waiver
靜默變成永久事實。

### Agent OS 的主要輸出是可提交轉移

模型文字、程式碼甚至 artifact 都是中間物。最終價值單位會是：一個有 provenance、已記帳、
符合 capability、帶足夠 assurance、且人能在需要時理解和撤回的 world state transition。
