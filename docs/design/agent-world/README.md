# Agent World 設計報告

這份是跨來源設計底圖；已收斂的慢速機率 machine、OS 資源管理與實作順序，見
[FreePy Agent Machine](../../freepy/future/agent-machine/README.md)。

本報告把三條原本分散的想法接成一套架構：Plan 9 的 per-process namespace、Linux-as-Lisp 的「歸一於路徑」，以及模型上下文可被提升成組織化記憶檔案。

這不是完成的 implementation spec，而是可繼續收斂的設計底圖。內容吸收了 `C:/code/mine/ai_core` 的正式九軸規範、`sub_projs/handy/notes` 思考鏈、OS-as-game 筆記與已跑通 9P 的 `unipath` 原型。來源的「定案／筆記／實證」在 [來源與可信度](SOURCES.md) 分開標示。

## 核心命題

> agent 是在私有 namespace 裡求值的 process；agentfs 是它看得見的世界；memory 是可掛載的持久結構；context 是每一步按需編譯出的 working set。

對應語彙：

```text
組織        = canonical agent tree
agent view  = evaluation environment / private namespace
授權        = 可被 mount 的 capability
工具        = typed callable node
記憶提升    = quote：span -> immutable object
記憶載入    = checked dereference
context     = 本 Step 的 manifest + working set
Step       = ask(...) -> model message
Round        = agent 啟動後到主動停止的一段活動
tick        = 可選的多 agent／世界狀態轉移與同步邊界
```

## 三層，而非一棵萬能目錄

```text
┌────────────────────────────────────────────┐
│ 認知層：trace → memory graph → context view │
├────────────────────────────────────────────┤
│ 主體層：agent / team / Round / task / tool   │
├────────────────────────────────────────────┤
│ 基材層：path / namespace / handle / provider│
└────────────────────────────────────────────┘
```

- 基材層統一路徑與檔案動詞，但不抹掉型別、權限和狀態機。
- 主體層把 agent 當有生命週期、資源、上下級與行動能力的 execution unit。
- 認知層保留不可變 trace，把舊段落轉成可尋址記憶，為每個 Step 編譯有限 context。

容器不在這三層裡。container、namespace、cgroup、seccomp 是 runtime 可選的強制隔離後端；agentfs 本身是 control/data projection，不因長得像檔案系統就自動成為 sandbox。

## 這次收斂出的新觀點

1. **先歸一於路徑，後成局。** 靜態資料、live process state、呼叫方式都可定址，之後才談 agent group 像一個世界。
2. **每個 agent 看見不同的樹。** `/root/a/b` 是唯一組織身分；`/self`、`/parent`、`/team` 是 actor-specific view。全域 path 不等於全域可見。
3. **path 是 symbol，open handle 才是 capability。** 知道名字不能取代授權；handle 應綁 actor、instance 與 grant generation。
4. **九軸成為節點契約。** 工具、agent、context compiler 和 memory derivation 都要說明 I/O、生命週期、狀態、資源、中斷、保證、組合、環境與隨機性。
5. **記憶不只是文件分類。** 它還是 evidence、mechanical facts、待審 grounding、accepted facts、derived conclusions 的依賴圖。
6. **context compact 是重新編譯 view。** 原始 user／assistant／tool trace 不被刪改；下一步只帶 inline blocks、link cards 與明確載入的 refs。
7. **同一內容可有多個 view。** task、topic、entity、chronology 是不同 mount/index，不應各複製一份正文。
8. **慢世界與快世界共用契約。** agentfs/9P 適合粗粒度組合；可信且熱的邊可走 in-process，exec/container 仍是隔離地板。

## 閱讀順序

1. [歸一於路徑與私有世界](01-path-world.md)
2. [九軸作為契約與推導系統](02-nine-axes.md)
3. [組織化記憶與 context 編譯](03-memory-context.md)
4. [Step、Round 與 tick](04-time-agency.md)
5. [檔案協定、能力與隔離](05-protocol-security.md)
6. [分期路線與驗收](06-roadmap.md)

瀏覽器入口是 [index.html](index.html)。目前實作順序見
[`freepy/ROADMAP.md`](../../../freepy/ROADMAP.md)，現行 Round 語意見
[`agentloop/ROUNDS.md`](../../../freepy/agentloop/ROUNDS.md)；agentfs 與 memory 的材料已移到
[`docs/freepy/future/`](../../freepy/future/README.md)，只代表延後候選設計。
