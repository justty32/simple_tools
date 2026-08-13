# FreePy 實作路線

這份只回答「接下來做哪一層」。目前分層的唯一入口是
[`docs/freepy/architecture.md`](../docs/freepy/architecture.md)；未來規格不能反過來宣稱自己是
現況 API。

## 由內向外

```text
agentloop：loop + Handle + Limits       原始本地核心
    └─ Controller                       較好用的本地 wrapper

interfaces / control adapters           REPL、CLI、GUI、Pi、MCP
foundation libraries                    llmkit、tooljson、read/write/exec/socket

durable services                        persistence、scheduler、Candidate、Goal…
runtime enforcement                     permission、sandbox、resource isolation
```

前兩層已達可用基線；2026-08-13 起開始實作外層 Agent Machine，但仍維持分層。`Controller` 不等於 durable control plane，
`agentloop.advance()` 也不產生 durable commands：它只在 owner runner thread 執行一個 Step 或一整批
tools，抵達下一個 safe boundary 後返回。

## 已完成的底座

- `llmkit`：一次模型請求、Reply、endpoint preset、tool schema。
- `tooljson`：工具描述、載入、參數轉譯與 dispatch。
- `base_tools`：檔案與 POSIX shell 基礎能力；不是 sandbox。
- `exec_tools`：從明確的工具目錄發現 specs；不掃 `PATH` 或自動猜 schema。
- `http_tools`：固定 endpoint 的 typed HTTP effect；不收模型提供的任意 URL。
- `agentloop`：Round／Step、Handle、callbacks、Limits、parked runner 與安全 pause/end。
- `advance()`：一個本地 operation；保留公開狀態在 parked boundary 的可編輯性。
- `Controller`：組合 Handle、前景／背景／逐步 runner 的第一版本地 API。
- Pi bridge：Pi extension 經 JSONL 控制獨立 Python agentloop；launcher 在 factory 明確設定時
  自動載入 extension，不借 Pi endpoint，也不接管 Pi loop。

## 現在主線：Agent Machine Python 參考實作

完整施工圖在 [`docs/freepy/agent-machine/`](../docs/freepy/agent-machine/README.md)。第一個里程碑拆成：
PR 1 做 `name/.name`、stable node id、immutable snapshot/root generation；PR 2 做
staging/journal/HEAD recovery；PR 3 加 derived Git checkpoint。之後才接 deterministic VFS executor，
不在這三個 PR 加真模型、scheduler、FUSE 或 Janet。

Python 版本要成為日後 C++23／Janet 的 contract oracle，因此每一層都先產生 versioned JSON vectors、
crash traces 與明確 gate。接 LLM/tool 時透過 adapter 使用既有 `llmkit`／`agentloop`，不持久化活的
Handle、callback、thread 或 callable。

Gate：在每個交易寫點模擬 crash 後，恢復結果只能是完整 G 或 G+1；duplicate operation 不重做，
stale generation 不覆蓋，`bot-a/.bot-a` 配對在 rename/move/delete 後保持一致。

## 維護線：讓本地層繼續好用

### 1. 以使用回饋演進 Controller

先用現有 `Controller` 做真實小任務，收集重複樣板，再決定哪些常用狀態、Limits preset、callback
組合值得升格。Handle 永遠保留；其他人也可在 Handle 上建立不同抽象。

Gate：Handle ownership、parked edits、pause/end boundary、並行誤用都有回歸測試；Controller 不暗示
database、ACL 或 sandbox 保證。

### 2. 做真正直接的 Python 互動入口

把「import、建 bot、掛 tools、啟動、查看、繼續」收成小型 Python library／REPL helpers。廣義上
GUI、vim、Pi extension 都是互動 shell；若取 Unix 薄殼原義，Python REPL／CLI 才是最接近的入口。

第一版已有 `shells.session()`、Controller `.send()` 與等到可介入／終止邊界的
`.wait()`，且 launcher 會預載常用名稱。
真實任務顯示 bot／tools 組裝值得收斂，因此增加 `shells.assistant()` 與 `toolbox()`；
`repl` 會保留操作者的 cwd 並顯示工具 workspace，`assistant(...).session(...)` 可直接啟動
配對好的 bot/dispatch；工具來源、workspace 與 effect policy 仍由呼叫者明確決定。

2026-08-12 的真實 REPL 操作進一步確定介面應直接表達
`Bot(LLM(...), system=..., tools=...).start(instruction)`：`Bot` 建構時就持有完整工具定義與
dispatch，不再引入 `Assistant`／`equip` 中間概念，`start()` 啟動一個背景 Round。完整命名、
tools 契約與目前 API 對照見
[`docs/freepy/interfaces/python-api-next.md`](../docs/freepy/interfaces/python-api-next.md)。第一版已於
2026-08-13 實作；`Engine`、`Assistant`、`assistant()` 與 `session()` 暫留為遷移相容層。

Gate：使用者在 REPL 只需少量語句就能啟動與控制 Round；底層 Handle 仍可直接取得。

### 3. 整理基礎函式庫

沿 `llmkit → tooljson → tools` 補齊底層能力；read/write、process 與第一版 typed network effect
已有基線，deterministic discovery 已落在 `exec_tools`，tool schema 與 exec recipe 也有
跨平台的載入期驗證；Python tool 的明確 `path` 也會拒絕同名模組／父 package 的來源衝突。每個
library 都要分開描述：資料格式、真正 effect、成本／不確定性，以及誰負責 permission／approval。

Gate：schema 與執行分離；工具來源明確；輸出有界；不把 root check 或 callback 說成 sandbox。

## 之後：control adapters

Pi bridge 已有第一版；MCP、Claude Code、Codex 等仍是設計資料。它們是跨 process lifecycle 與
protocol adapters，不擁有 agentloop 真實狀態。只有共同操作成熟後，才抽共享 server/API。

Gate：protocol 錯誤、timeout、child cleanup、allowlisted edit 與 host lifecycle 有離線證據。

## 之後：runtime、memory、team、projection

permission 與 sandbox 是獨立的 runtime 邊界；endpoint／token／context 要有資源管理；memory 要有
來源、ACL 與 context budget；team 要有 identity、grant 與資源守恆；agentfs 只能投影既有真源。
先由 Agent Machine 的 VFS/transaction/executor/scheduler 切片確立邊界，再逐一接入；不在第一個
Python PR 同時開工。
