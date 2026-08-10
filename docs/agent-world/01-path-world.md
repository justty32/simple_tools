# 歸一於路徑與私有世界

## 不是「一切存成普通檔案」

Linux-as-Lisp 最有用的版本是 **unify-to-path**：

1. 靜態物可用路徑找到：定義、資料、記憶、artifact。
2. 執行中的東西可用路徑找到：agent、Round、tool run、resource state。
3. 呼叫方式可用路徑找到：tool entry、`ctl`、`clone`、inbox 等 protocol nodes。

路徑統一的是命名和導航，不是儲存媒介。`/self/round` 可以由記憶體 provider 即時計算；`/memory/objects/<hash>` 可以來自 object store；`/tools/web/clone` 可以是 supervisor 操作。它們服從同一組 walk/stat/read 契約，不必真的落成宿主檔案。

## 唯一身分與私有 view

canonical agent identity 使用唯一組織樹：

```text
/root
/root/research
/root/research/web
/root/build/tests
```

但 `/root/research/web` 實際看到的是 supervisor 組成的私有 namespace：

```text
/self       -> 自己的 live interface
/parent     -> 經 grants 過濾的主管 view
/children   -> 自己可管理的 children
/team       -> 本 task 獲准看見的 peers/services
/tools      -> 被授予的 callable nodes
/memory     -> 可讀的 memory views
/work       -> workspace 的受限 projection
/srv        -> 可 attach 的命名服務
```

這和把共同實體目錄 bind-mount 給所有 agent 不同。每個 session 必須由可信 supervisor 綁定 actor identity；client 不可在 request 裡自行宣稱 `actor=/root`。

`/root/...` 解決「我是誰」；`/self/...` 解決「我現在看見什麼」。兩者不可混成一條規則，否則組織 path 會意外變成 authority。

## symbol、object、handle

可用 Lisp 的三個層次理解：

| 層 | 例子 | 性質 |
|---|---|---|
| symbol/path | `/tools/search` | 可分享、可重找，但會重新解析 |
| object identity | agent instance id、memory hash | 穩定指向某個版本／實體 |
| open handle/fid | 已 attach/open 的 session handle | 綁 actor、權限、generation 的 live capability |

因此 long-running operation 不應每一步只重傳 path。open 時應固定 instance 與授權世代；agent 被重啟、grant 被撤回或 object 換版時，舊 handle 要失效或明確顯示 stale，不能悄悄指到新人。

## 目錄不是天然的 list

檔案系統目錄本質較像 map：名稱到 child；Lisp list 則帶位置和順序。需要順序時要顯式表示：

- `/steps/0`、`/steps/1`：0-based numeric segments，適合 append-only sequence。
- `manifest.json`：保存 block 的精確順序與版本，適合 context。
- stable id + order field：避免刪一項後全部改名。

分類目錄則是 view，不是唯一所有權。例如同一 memory object 可同時掛到：

```text
/memory/decisions/sandbox/m42
/memory/tasks/T17/m42
/memory/topics/plan9/m42
```

三條都解析到同一 immutable object。移除某個分類不刪正文。

## 慢世界與快世界

路徑／9P 適合人會單獨命名的粗粒度物件：agent、tool run、task、記憶段落。不要把每個 token 或 AST cell 都做一次 filesystem operation。

```text
慢世界：path + file protocol + process boundary
         可觀察、可組合、易隔離、成本較高

快世界：in-process Value / native call
         低延遲、強耦合、失去 process isolation
```

兩者共用 logical schema 和 provenance，不要求共用物理表示。跨 process 預設走可序列化 Value/JSON；只有「可信、支援、確實是熱路徑」才升級 in-process。exec 是可攜且有隔離的地板，不是落後模式。

## `unipath` 已證明與尚未證明的

`ai_core/sub_projs/unipath` 已用 Python、C++、Fennel 的 9P server 與同一 client 驗過：live object 能以路徑 walk/read/write，核心 v9fs 可掛載，`ctl/data/status` 形狀可跨語言，tick 規則也能住在樹上。

它尚未證明 production agentfs 的幾件難事：

- 多 actor 的 per-view namespace 與 ACL。
- instance replacement、revocation 和 audit。
- secret broker 與 untrusted content。
- Windows 開發環境的 adapter。
- 大量 agent／memory nodes 的成本與一致 snapshot。

因此正確延伸不是直接搬 FUSE 程式，而是先抽出 provider 與 view contract，保留 9P 作為已驗證的 transport 選項。

## 設計規則

```text
path 負責尋址
schema 負責語意
handle 負責能力
provider 負責資料真源
supervisor 負責狀態轉移
namespace 負責最小可見世界
```

若一項設計要求 path 同時承擔這六件事，表示抽象已經過載。

