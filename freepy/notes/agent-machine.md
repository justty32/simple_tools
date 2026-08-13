# Agent Machine 構想

本頁記錄 2026-08-13 提出的架構願景。目標是參考現代計算機體系，統一並抽象掉實際硬體與
執行後端的差異，再結合 Plan 9 與 Lisp machine 的精神，形成適合 agent 的 machine／OS
模型。這目前是探索方向，不代表近期 runtime API 已經定案。

## 定位：現有 OS 之上的理想 machine layer

Agent Machine 不取代 Linux、Windows 或其他現有 OS，也不重新實作底層硬體管理。它建立在
host OS 已有的 process、filesystem、network、device、permission 與硬體 CPU 之上，再提供一層
為 agent 設計的理想化 machine abstraction。底層 OS 繼續負責真正的機器資源；Agent Machine
負責把較高層的執行者、函數、狀態與資源呈現成一致模型。

其中最重要的抽象是 CPU。這裡的 CPU 不是常規硬體 CPU，而是能執行一類 agent instruction
的上層 executor：LLM endpoint 是一類 CPU；封裝過的 read／write、shell、program、service
或 device operator 也是其他類型的 CPU。它們最終仍由現有 OS 與硬體執行，但 Agent Machine
只面對統一後的 instruction dispatch、狀態轉移、成本與 scheduling interface。

與此相對，Agent Machine 將傳統記憶體與磁碟規劃成同一個統一檔案空間。上層函數只面對
可命名、可尋址、可 read／write 的 resources，不以資料實際位於 RAM、page cache、本機磁碟
或遠端儲存作為主要介面邊界；placement、cache、持久性與 materialization 由下層 policy 處理。

## 計算模型

- LLM endpoint 是一種 CPU。
- 一次 LLM 呼叫就是一條 CPU 指令。
- system prompt、message history、tools，以及傳給 endpoint 的各類 LLM 設定，都是這條
  指令的參數。
- tool execution 也是一條 CPU 指令，只是使用不同類型的上層 CPU；它可能是包裝過的
  read／write，或由 shell、Python、遠端服務及其他 executor 執行，而不是由 LLM endpoint
  執行。
- 一個 Round 是一個函數。它由多次異質 CPU 指令組成，並具有自己的參數、執行狀態與結果。

換句話說，LLM 與 tool 不應被視為兩種完全不同的 runtime 特例；兩者都是 machine 可以
dispatch 的指令，只是 target CPU／executor、成本、延遲與效果不同。目前的 `LLM`、`Bot`、
`Round` 與 tool dispatch，可視為這套抽象的初步元件。

在這個模型中，Agent Machine 的狀態就是虛擬檔案系統的狀態。因此 CPU 指令執行並產生影響，
其可觀察結果最終就是對檔案系統造成影響：建立、讀取、修改、移動或刪除檔案，以及更新其
metadata。LLM 生成的 message、Round state、tool result 與 refs 更新都以這種方式 materialize，
而不是另藏一份只有 runtime 知道的權威狀態。

## 記憶體與檔案系統

Bot 已可透過 tool 的 read／write 存取檔案，因此將記憶體與磁碟統一規劃成 agent machine 的
檔案空間，並把這個 filesystem 視為其記憶體布局：

- path／resource identifier 類似位址或指標。
- read 是指標取值，將取得的內容納入下一條指令的參數。
- write 是對該位址更新狀態。
- message history、工作檔案、長期資料與外部資源，可以位於同一個可命名、可讀寫的空間，
  而不必先切成彼此無關的「context」「memory」「storage」特例。

對 LLM 這種本身就很慢的 CPU 而言，傳統 RAM 與磁碟的延遲差距未必是首要邊界；磁碟可以被
理解為較慢的記憶體，兩者先以一致的 addressable resource 抽象處理，再由實作層表達 latency、
容量、持久性與 caching 差異。

## 一切資源皆可讀寫

沿用 Plan 9 的方向，網路、周邊設備、服務與其他資源也暴露成可命名的 read／write 介面。
從 agent machine 的角度，它們與現代 OS 對記憶體位址或 device resource 的存取沒有本質差別：
都是對某個位址讀取狀態，或向某個位址寫入資料／命令。

這裡的 read／write 是統一介面，不表示所有操作都沒有副作用。permission、capability、locking、
transaction、失敗語意與 sandbox 仍須由 resource／OS 層明確管理。

## 函數檔案與 companion directory

Bot／函數不另外投影到 `/proc`；它直接住在自己的 namespace node。函數一開始可以只是一個
可執行檔案，例如 `somefile`。隨著它承擔的職責變重、持有更多資料與狀態，原檔案仍保持為
同一個單一檔案；擴張出的結構放在同目錄的 `.somefile/` companion directory。這避免把檔案
轉成目錄，也讓既有呼叫路徑與檔案 identity 保持不變。

例如某個 Bot 可能呈現為以下形式；路徑只用來說明配對規則，不代表規定採用 Unix filesystem
layout：

```text
bot-a
.bot-a/llm
.bot-a/system_prompt
.bot-a/message_history
.bot-a/tools
.bot-a/parameters
.bot-a/state
.bot-a/refs
```

`bot-a` 是可直接執行的函數；`.bot-a/` 保存它逐步增加的資料、狀態與子功能。兩者共同構成
函數的 filesystem representation，但公開 callable identity 仍是 `bot-a`。不需要另一棵 process
hierarchy，也不需預先替所有未來用途設計完整目錄樹。

如此一來，CPU 執行一條 LLM 或 tool 指令，就是對函數狀態進行一次 state transition：讀取
目前 Bot 檔案、companion directory 與其他 resource files 作為指令參數，執行後再更新
history、state、result、
resource references 等檔案。scheduler、operator 或另一個有權限的 agent，也能透過相同介面
觀察、暫停、恢復或調整函數，而不需要另一套只存在於 runtime 內部的控制協定。

Bot 的 companion directory 不只是資料或 debug projection；若允許 write，它也是 machine 的
control surface。
因此 snapshot consistency、欄位的原子更新、寫入驗證、權限與哪些檔案只讀，都必須成為
正式契約。

## Garbage collection：未被函數引用的檔案

借用 Lisp machine 的垃圾回收概念，filesystem 中的檔案形成 object graph，而存活函數所持有
的引用形成 GC roots。每個 Bot／Round 透過 companion directory 中的 `refs`（如
`.bot-a/refs`）記錄
目前引用的檔案或其他
addressable resources；凡是不能從任何存活函數、系統保留根或明確 pin 的資源抵達的檔案，
就是 agent machine 中的垃圾。

中央管理 scheduler 定期掃描 refs、判定 reachability，並依 policy 將垃圾刪除或封存。封存可
作為較保守的預設回收階段：先移出 active namespace、保留 provenance 與恢復期限，確認不再
需要後才永久刪除。這讓短期工作檔、tool output 與中間推理產物可以自動管理，而不必由每個
Bot 各自實作清理流程。

`refs` 因而不只是資訊列表，也是 lifetime contract。CPU 執行造成函數狀態轉移時，
必須同步更新 refs；scheduler 只能回收一致 snapshot 中確定不可達的資源。正式設計需要處理
執行中暫存引用、跨 Bot 共享、循環引用、租約／pin、正在進行的 read／write、封存恢復，以及
外部資源不能真的由本機刪除等情況。

## Shell：呼叫函數的文字介面

Shell 基本上就是普通的 OS shell，不需要另一套特殊抽象。它是提供給使用者的文字指令介面，
讓人以路徑尋址並呼叫函數。

例如目前位於 `/root/home/mine/`，可以寫：

```sh
./bot_a/start "do something"
```

其中 `./bot_a` 指向一個 Bot／函數，`start` 呼叫它，`"do something"` 是本次輸入。其餘沿用
一般 shell 已有的 path、arguments、pipes、redirection 與 process 操作語意即可。

關鍵不在 shell 本身增加新能力，而在 Plan 9 式「一切皆檔案」的 machine interface：Bot、
Round、LLM、prompt、history、refs、網路與設備都能透過 namespace 存取，因此普通 shell
原有的檔案操作與組合能力，就足以觀察、設定、啟動、連接和管理 agent functions。能力來自
一致的 filesystem interface，shell 只是自然承接它。

## 排程與並行

既然 Round 是函數、LLM call 與 tool execution 是異質 CPU 指令，就可以借用現代 OS 的 CPU
排程方法來管理多個 Round 的並行：

- scheduler 決定哪個 Round 取得哪一類 executor，以及何時執行。
- waiting、paused、running step 與 running tools 可對應 scheduler 可觀察的執行狀態。
- endpoint concurrency、tool capacity、token／時間預算與外部資源鎖，成為 scheduling constraint。
- context／history 與 filesystem-backed state 構成函數執行所需的狀態；切換 Round 類似 context
  switch。

排程成本不只包含傳統的記憶體、CPU／wall time、I/O 與 executor capacity。Agent machine 還要
直接考慮：

- **確定性**：某項工作需要可重現、可驗證的 tool／program，還是可以接受具隨機性與不完全
  可靠的 LLM；必要時應優先選擇更確定的 execution path。
- **Token 消耗**：prompt、history、generation 與重試的 token 預算，以及其對 throughput 和
  金錢成本的影響。
- **使用者體驗**：首個可見結果的延遲、整體完成時間、互動工作的優先權、progress visibility、
  streaming、暫停／取消能力，以及背景工作是否阻塞前景工作。

因此 scheduler 面對的不是單一「最快完成」目標，而是依工作與 policy 在正確性／確定性、
成本、延遲、公平性和使用者感受到的 responsiveness 之間取捨。這些應是 admission、executor
selection、priority、preemption 與 retry 決策的正式輸入，而不只是執行後才收集的 metrics。

這使多 agent 或多 Round 不必先發明另一套特殊協調機制，而可先從成熟的 OS scheduling、
isolation、resource accounting 與 synchronization 概念中取用合適部分。

## 第一個實作基礎：虛擬檔案系統

要實現上述計算模型，首先需要做一個虛擬檔案系統（VFS）。它是 Agent Machine 統一 namespace
的具體承載層，先讓不同來源、生命週期與儲存位置的 resources 都能用一致的 path、read、write
與 metadata 介面呈現。

VFS 至少要能掛載：

- Bot／Round 自有目錄中的資料、狀態與控制介面。
- 本機工作檔案，以及由 host OS filesystem 提供的持久資料。
- LLM、tool、network、service 與 device 等虛擬 resources。
- 各 Bot companion directory 中 `refs` 所指向、可供 GC 追蹤的 objects。

第一階段的 VFS 不必自己實作實體儲存或取代 host filesystem；它可以是現有 OS 之上的 namespace
與 adapter layer。重點是先定義統一的 identity、path resolution、read／write、metadata、權限、
mount 與 lifetime 語意。函數呼叫、shell 組合、refs／GC 和中央 scheduler 都建立在這個共同介面
之上。

VFS 同時也是 machine state 與 effect boundary：executor 可以有內部暫存，但一條 CPU 指令
commit 後的正式效果必須反映到 VFS。這使狀態觀察、持久化、追蹤、回復與排程都能針對同一份
machine state 工作。

### Namespace 不遵循既定 Unix layout

這個 VFS 不預設遵循 Unix 的 `/bin`、`/etc`、`/home`、`/proc` 等布局。它從實際存在的函數與
資源開始，一點一點生長；結構是職責與使用歷史的結果，而不是事前套入的固定分類。單一
可執行檔案在需要保存更多狀態、子功能或控制入口時，保留原檔並增加同層的 dot-prefixed
companion directory；目錄內的函數也能以相同規則繼續生長。

這套遞迴表示也適用於 Agent Machine／OS 自身。回到作業系統的本質，它的目標是讓一個或多個
人執行他們的多個程式；從模型上看，它也是一個較大的函數，輸入是使用者指令、檔案內容、
事件與其他大量且多變的參數，輸出與 effects 則反映到檔案系統。既然 OS 也是函數，它不需要
特殊的表示法：同樣可從一個可執行檔案開始，隨職責增加而擁有自己的 companion directory。

## 第二個實作基礎：Git

VFS 之後，第二個基礎是引入 Git。既然 Agent Machine 的權威狀態與 CPU effects 都落在檔案
空間，Git 可以直接成為 machine state 的版本層，提供：

- 每次狀態轉移前後的 diff 與 provenance。
- 歷史查詢、checkpoint、rollback 與 recovery。
- branch／merge，作為平行嘗試、推測執行或多個函數修改同一工作空間的基礎機制。
- 對使用者與 scheduler 都可見的變更審計，以及判斷哪些產物仍被歷史引用的依據。

檔案系統模型從這裡開始真正發揮威力。因為函數、狀態、引用與 effects 已經使用同一種表示，
Git 不再只是管理 source code，而能直接版本化 Agent Machine 的可觀察狀態。既有的 diff、
commit、branch、merge 與 checkout 因而自然成為 machine-level primitives，不必為 agent state
另外打造彼此不相容的 history、snapshot 與 collaboration systems。

Git 不取代 VFS；VFS 定義 live machine state 與統一操作介面，Git 負責對其中適合納入版本控制
的狀態建立時間維度。並非所有 mounted resource、暫存資料或外部副作用都能直接 commit，這些
需要由 mount／resource policy 決定如何記錄。

CPU instruction、Round、user-visible operation 與 Git commit 之間的 boundary 尚未定案。理想上
commit 應對應有意義且可恢復的原子狀態轉移，但不預設每一次 read 或每一條內部 instruction
都必須產生獨立 commit。

## 內建機制：Agent 自我改進

Agent 自我改進是 Agent Machine 的內建機制，不是某個 Bot 額外安裝的特殊功能。函數本身、
system prompt、tools、參數、測試與狀態都以 VFS nodes 表示，因此 agent 能使用與修改其他檔案
相同的機制讀取並修改自身表示。

VFS 與 Git 讓自我改進具有 machine-level workflow：從目前版本建立 branch，在隔離空間修改
函數或其組成檔案，執行測試／eval，比較確定性、能力、token 成本與使用者體驗，通過 policy
後才合併為新的 active version；失敗或退化則保留證據並 rollback。中央 scheduler 可以安排
這類背景改進工作，並限制其 executor、budget、priority 與可修改範圍。

「內建」不表示任何 agent 都能任意重寫一切。修改權限、不可變 invariants、evaluation gate、
approval、provenance、版本 pinning 與 recovery 都是 machine policy 的一部分。自我改進的核心
是讓提出變更、驗證、採用與回退成為一致且可追蹤的系統操作。

## 後續設計焦點

未來開始實作 agent machine 前，需要把上述類比收斂成可驗證契約，至少釐清：

- VFS 的最小 protocol、mount model、path／object identity 與 host filesystem adapter。
- Git repository／worktree layout、commit boundary、branch lifecycle、merge/conflict 與外部副作用
  的記錄方式。
- 自我改進的 mutation scope、eval／acceptance gate、版本晉升、權限與 rollback policy。
- instruction 的共同表示，以及 LLM／shell／其他 executor 的差異應留在哪一層。
- Round 作為函數時的輸入、回傳、暫停、恢復、取消與失敗語意。
- executable／dot-prefixed companion directory 的配對、移動、命名衝突與 visibility 規則。
- companion directory 的狀態檔 schema，以及多檔案更新是否需要 transaction。
- companion directory 中 `refs` 的 root／reachability 規則、引用更新的原子性與
  delete／archive policy。
- resource namespace、read／write protocol、capability 與 side-effect classification。
- filesystem、message history 與 endpoint context window 之間的 materialization 規則。
- scheduler 的公平性、優先權、backpressure、資源配額、確定性、token 成本、UX policy 及
  不同 executor 的並行限制。
- 指標／位址失效、並行讀寫、鎖與 transaction 如何表達。

近期仍先完成既有 `Bot.set_llm()` safe-boundary 契約；本頁作為其後 agent machine 設計工作的
起點，而不是現在立刻擴張 runtime scope 的承諾。
