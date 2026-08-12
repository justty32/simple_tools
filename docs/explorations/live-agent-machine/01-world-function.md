# 世界作為函數

## 已經發生的事

人們今天操作 coding agent 時，會指定一個 workspace，讓它讀程式、文件與設定，呼叫工具、
修改檔案、跑測試，再依結果繼續。這表示模型的實際輸入從來不是孤立 prompt：prompt 只選出
本次目標，workspace 與執行中的變化才提供函數體和環境。

普通函式常被想成：呼叫前定義完成，傳入參數後一路執行到 return。但現行
[`Handle`](../../../freepy/agentloop/README.md) 已允許在 Round 中加入 instruction、image、tool 或
ask options；這些變更從下一 Step 起生效。函數與呼叫者在求值期間仍維持互動，已不是
「送出 prompt 後放手」的模型。

## 一個較精確的對應

| Agent world | Lisp／語言概念 | 工程限制 |
|---|---|---|
| path | symbol | 只負責尋址，不是授權 |
| directory | module／compound form | 順序、型別與入口必須顯式描述 |
| private namespace | evaluation environment | 每個 actor 看見的 binding 不同 |
| mount | bind／parameter injection | semantic binding、OS mount、projection mount 要分開 |
| open handle | resolved/captured reference | 綁 actor、instance、grant generation |
| workspace snapshot | immutable function version | live disk 本身不是純值 |
| prompt | entry form／本次 invocation | 不是完整程式 |
| context manifest | ordered read set／address space | 固定某 Step 實際看見的內容 |
| Round | 有生命週期的求值活動 | 可暫停、追加、停止；不是 goal 本身 |
| Step | stochastic reduction | 已送出後輸入不可暗中改變 |
| tool intent | effect proposal | 驗 capability 後才可執行 |
| test／verifier | postcondition checker | 檔案改動不自動等於完成 |
| Git commit／object | quote／frozen definition | branch 是新求值分支，不是 replay |

這延伸了現有 [path／object／handle 三層](../../design/agent-world/01-path-world.md)，沒有取消
「path 不是 capability」與「projection 不是本體」兩條安全邊界。

## Workspace Form

資料夾不天然是 AST：它沒有可靠順序，也混有 source、cache、secret、artifact 與 untrusted
data。因此可以在普通 workspace 上編譯出一層 **Workspace Form IR（WFIR）**，而不是發明另一個
虛擬檔案格式：

```text
workspace bytes + structure + tool specs + policy + goal + generation
                              │
                              ▼
Workspace Form {inputs, exports, reads, writes, mounts, postconditions}
                              │
                 ┌────────────┴────────────┐
                 ▼                         ▼
          Step context manifest       typed effect graph
```

概念資料如下：

```json
{
  "kind": "workspace.form/v1",
  "root": "workspace:demo@g17",
  "entry": ["instruction:current", "work:README.md"],
  "reads": ["work:src/**", "memory:design-42"],
  "writes": ["work:src/**"],
  "mounts": ["tool:test", "tool:build"],
  "postconditions": ["tests-pass", "diff-reviewed"]
}
```

它是 declared form；supervisor 仍計算 effective form，runtime 再記 observed reads、writes、cost
與 evidence。這直接沿用 [declared／effective／observed](../../design/agent-world/02-nine-axes.md)，避免
「文件寫可寫」被誤認成實際有權寫。

## Linux 為何是編輯器

若 function 的可見表示就是 workspace tree，那麼 create、edit、rename、link、mount 與 process
輸出，確實都是在改寫 form 或 environment。VS Code、shell、檔案瀏覽器、Git 和未來 GUI 只是
操作同一 image 的不同 client；Linux/VFS 提供共同的 naming、composition 與 effect substrate。

但「Linux 是編輯器」不等於「磁碟是權威 reducer」。比較安全的分工是：

```text
Linux/VFS       materialize、observe、enforce 實際檔案與 process effect
Agent Machine   驗證 intent、記 generation/event、提交 logical image
Workbench       讓人理解、比較、介入並呼叫相同 typed operations
```

這個區分能容納外部 editor：人直接存檔後，supervisor 將變化正規化為 `WorkspaceDelta`，附上
來源、before/after hash 與 generation；下一 Step 才明確選擇是否載入。它不把 inotify 事件原文
直接塞進 prompt，也不讓半次寫入冒充新函數定義。

## Closure 不只是一個資料夾

真正交給 agent 的 closure 至少包含：

```text
workspace root + private namespace + capability handles
+ goal/postconditions + policy + memory refs + resource leases
+ current Round continuation + source/event head
```

因此更準確的說法不是「一個實體目錄就是全部 agent program」，而是「受版本與權限約束的
world view 是程式；資料夾是它最重要、最通用、可由 Linux 編輯的表示」。這仍保留原始洞見，
也能解釋為何單檔文字編輯器逐漸力有未逮。
