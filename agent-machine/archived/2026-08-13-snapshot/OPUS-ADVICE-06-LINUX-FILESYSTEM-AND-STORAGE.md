<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-05-GAPS-DOCS-AND-BUILD-ORDER.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-07-FILE-TOOLS-AND-OPEN-DECISIONS.md)
<!-- archive-nav:end -->

<!-- archive-original:start -->
## 十、捨棄自製 VFS 之後（2026-08-13 定案）

**決定：`agent-machine/` 的 CLI 設計是主線；不做自製 VFS，直接用 Linux filesystem。
VFS 只作為概念保留，在特定操作上引用。**

這是對的決定，而且它自己會回答上面好幾個問題。但「概念隱性保留」有一個特定的失敗模式：
概念不寫下來，就會被**不一致地套用** —— 這個操作做了 CAS-like 的重驗，那個沒有，而沒有規則可查。
所以建議在 `RUNTIME.md` 放一張對照表，讓層次消失、但對應關係還在：

| VFS 概念 | 在 Linux 上的實際對應 | 現在有嗎 |
|---|---|---|
| root generation | journal 的遞增 `seq` | 有（`OUTPUT.md`） |
| CAS | state lock 下重新驗證再 commit | 有（`RUNTIME.md` model mutation 那段） |
| VFS transaction | temp → fsync → atomic rename → fsync parent | 有（`STORAGE.md`） |
| node identity | **沒有，path 就是身分** | 見下 |
| refs / GC | archive 與 journal 的保留政策 | **缺** |
| Git checkpoint | 使用者自己 `git init`，只版本化定義 | **缺** |

### 10.1 沒有 node_id 的代價，全部集中在 `$ref`

捨棄 node identity 幾乎沒有損失 —— 因為 Bot 沒有被任何東西以 ID 引用，`mv bot-a bot-b` 直接成立。

唯一的例外是 `$ref`。`CONFIG.md` 說 ref 存成相對 Bot root 的路徑「讓 Bot 目錄搬家時盡量仍可用」，
但 `../shared/ollama.json` 這種跨出 Bot 的 ref，**只要 Bot 搬到別的層級就一定斷**，
而且斷掉的時間點是下一次 `start`，不是搬的當下。

這正好又是第 5.1 / 3.3 節那個建議的第二個理由：**能用名字就別用路徑**。

```sh
./llm use lm-qwen3.5-9b     # 查已安裝 catalog，跟 Bot 在哪無關
./tools add read_file       # 同上
./tools add ./my-tool.json  # 自訂的才用路徑，而且留在 Bot 內部
```

路徑 ref 保留給「Bot 目錄自己內部」和「使用者明知在做什麼」的情況。
另外 `agentos clone` 之後應該立刻 `check` 一次並回報斷掉的 ref，不要等到第一次 start。

### 10.2 `.agentos/` 現在混了兩種壽命完全不同的東西 ← 建議調整

Git 現在「就是 Git」了：Bot 是普通目錄、普通 JSON，`git init` 直接可用，
不需要任何整合。這其實免費拿回了 VFS 願景裡最有價值的那一半（版本化 Bot 的**定義**）。

但目前的 layout 讓這件事做不成。`STORAGE.md` 把三份 raw manifest 放在 `.agentos/`，
而 `RUNTIME.md` 把 state、journal、results、lock 也放在 `.agentos/`：

```text
.agentos/
  llm.json  messages.json  tools.json   ← 幾乎不變，是使用者真正想 diff 的東西
  state.lock worker.lock                ← 每次跑都變
  current/{state.json,journal.jsonl,results/}
  archive/  worker.log
```

值得進 git 的和一秒鐘變十次的東西同一層。建議分開：

```text
.agentos/
  llm.json  messages.json  tools.json   ← 定義
  run/      archive/  *.lock  *.log     ← 執行期
```

`.gitignore` 就變成一行 `.agentos/run/`（加 `archive/`）。`agentos clone` 也剛好是
「複製目錄，去掉 `run/` 和 `archive/`」。

**而且 `messages.json` 本身也該拆。** 它現在同時裝 `source`／`system`（定義，穩定）
和 `history`（執行期，每條 message 都在長）。這造成三個問題：每次 append 都要重寫整份
遞增的 JSON array、crash-safe 的難度比 append-only 高得多、git diff 永遠是噪音。

`STORAGE.md` 自己已經說「history 是 opaque runtime data」——storage layout 應該反映這句話：

```text
.agentos/messages.json        format / source / system     ← 定義
.agentos/run/history.jsonl    append-only 的 user/assistant/tool
```

這同時讓「一條 instruction 落盤」變成單純的 append，跟 journal 同一種寫入模式。

### 10.3 GC 降級成保留政策

沒有 object graph 就沒有 reachability 可算，refs/GC 整個子系統的正確翻譯是**一行保留政策**：
`archive/` 只留最近 N 次、`journal.jsonl` 超過某大小就轉存。第六節提過，這裡只是說明它的來歷 ——
它不是「還沒做的 GC」，它就是 GC 在這個決定下的最終形態，不要留著等以後補。

### 10.4 三份 agent-machine 文件的世代關係要標清楚

現在 repo 裡有三代同名計畫，每一代都聲稱是主線：

```text
docs/freepy/future/agent-machine/     ← 已被下一代明講「不再決定實作順序」
docs/freepy/agent-machine/            ← 自稱「2026-08-13 起成為主線開發」（VFS/generation/Git 計畫）
agent-machine/                        ← 實際主線
```

第二代的 `04-python.md` 現在會把人導向錯的施工順序（P0–P3 先蓋 namespace/CAS/Git 才接 LLM），
而且它的 P0 gate 要求實作已被放棄的 `name`/`.name` 配對。請在它的 README 開頭加一句
「已由 `agent-machine/` 取代；本目錄保留作為研究模型」，跟當初它對待 `future/` 的方式一樣。

第九節建議的施工順序反轉，現在不是我的偏好，而是這個決定的直接後果：
P0–P3 那三個 PR 保護的正是被捨棄掉的那層東西。

---

<!-- archive-original:end -->

<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-05-GAPS-DOCS-AND-BUILD-ORDER.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-07-FILE-TOOLS-AND-OPEN-DECISIONS.md)
<!-- archive-nav:end -->
