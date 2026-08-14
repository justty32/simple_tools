# 設計審查意見（Opus）

審查範圍：`agent-machine/` 全部 17 份 .md、`freepy/notes/agent-machine.md`、
`docs/freepy/agent-machine/`，以及 freepy 既有的 `llmkit/llms`、`llmkit/tooljson`、`agentloop`。
評判標準：KISS、使用者體驗、上手難度。

本頁是意見，不是契約。

---

## 總評

介面設計本身是好的。「Bot 是目錄物件」「一次 LLM call 或一個 tool call 就是一條 instruction」
「只有 `ask_user` 才 waiting」這三個決定都對，而且互相自洽。

問題不在介面，在**比例**：

```text
2113 行契約文件  ←→  689 行原型程式（而且實作的是舊介面）
```

17 份文件全部用「必須／拒絕／fail closed」的口氣寫成，描述一台還沒跑過一次的機器。
這些規則裡有相當比例會在真的實作時被推翻，而每推翻一條就要改 3~4 份互相引用的文件。
現在的最大風險不是設計錯，是**規格債**：文件已經重到會拖慢實作，而它保護的行為一次都還沒被驗證過。

三句話總結建議：

1. **刪掉 model unload／model lock／direct Ollama adapter** —— 這一條就砍掉全套文件約 20% 的複雜度，
   而且 freepy 早就用 LiteLLM proxy 解決了同一個問題。
2. **tools 格式不要自己再寫一份** —— `tooljson` 已經有規範、實作和 45 關測試，而現在這兩份
   `_extra` 規格已經**同版本號、不同語意**了（見下節，這是我認為最急的一個）。
3. **文件 17 份 → 6 份**，並且補一份真正的 5 分鐘 quickstart。現在沒有任何一條路徑能讓新使用者
   在讀完 2000 行之前跑出第一個答案。

---

## 一、做得好、不要動的部分

先講不該改的，免得下面的刪減建議被誤讀成整體否定。

| 決定 | 為什麼是對的 |
|---|---|
| 一次 instruction = 一次 LLM call 或**一個** tool call | 比 agentloop 的「整批 tools」邊界更細，逐步把關才真的可用。這是本設計勝過既有實作的地方 |
| 一個 Bot 一個 active Run | 省掉 Run ID、current pointer、history merge。正確的 v1 取捨 |
| 普通文字＝done，只有 `ask_user` 才 waiting | 可測、不猜語氣。**我完全同意這個提案，不必再猶豫** |
| messages 屬於 Bot 不屬於 Run | 所以「done 之後再 start」自然延續對話，不需要額外的 session 概念 |
| history 與 tool arguments 是 opaque，不跑 `$ref`／`$env` | 這是整份設計裡最有價值的一條規則。模型吐出的 `{"$ref": ...}` 被當成 include 展開會是很難查的安全漏洞 |
| tool schema 的 `$ref` 不是 include，v1 直接拒絕 | 同上，而且避免了第二套 file/network resolver |
| tool result 的固定第一行模板 | 錯誤字串會進 context 影響下一次生成，值得寫死 |
| journal 是真源、state 是 snapshot | 對 |
| 不假裝 rollback、不假裝 sandbox、`unknown` 不自動重送 | 誠實。這幾段一個字都別改 |

`skip REASON` 的設計其實比文件寫的更好用，但文件沒點破 —— **reason 會被模型看到，所以 `skip` 本身就是指示**：

```sh
./run skip "太危險，改成先讀檔案確認"
```

這一句同時完成了「拒絕這個 tool」和「告訴它改做什麼」。文件應該明講，否則使用者會以為要先 skip 再 send。

---

## 二、最急的一件事：`tooljson` 格式已經分岔

`MEMBERS.md` 說「v1 沿用 FreePy 已驗證的 `tooljson` 外形」，但 `EXEC.md` + `TOOL_SPEC.md` 實際上是
**重寫了一份**。兩份規格 85% 相同 —— 這讓剩下 15% 更危險，因為它們看起來可以互通。

而且兩邊的 `_extra._version` 都是 `"0.1.0"`。

| 項目 | `tooljson/EXEC.md` | `agent-machine/EXEC.md` | 後果 |
|---|---|---|---|
| `cwd: null` | **繼承呼叫端 cwd**，文件明講「不預設成 .json 所在位置」 | **Bot root** | 同一份 spec，模型給的相對路徑解讀到不同目錄。安靜地錯 |
| `_extra.source` | 有這個 key（size/mtime/sha256，判斷 spec 過期） | `_extra` 拒絕未列 key | **所有 tooljson 產生的 spec 都會被 AgentOS 拒絕載入** |
| exit code 不在 `ok_exit` | 開頭加一行 `exit N` | `Error [exit]: N` | 模型看到的字串不同，fixtures 不能共用 |
| 模型送 `null` | 等同沒給，跳過 | 型別不合，拒絕 | 行為相反 |
| `additionalProperties` | 範例未寫 | **必須明寫 `false`** | 既有 spec 全部不合格 |
| number → argv | 「照 JSON 的**字面**寫法」，`1.5` → `1.5` | 「使用 **JCS** number」，`1.0` → `1` | 字面 `1.0` 是 `1.0`，JCS 是 `1`。**組出不同的命令列** |
| `_type` | 開放註冊，內建 `exec`／`python` | 只收 `exec`／`builtin` | 這個縮限本身合理，但要有自己的版本號 |

建議（依偏好排序）：

1. **直接依賴 `tooljson`**，`agent-machine/EXEC.md` 和 `TOOL_SPEC.md` 刪掉，只留一頁
   `TOOLS.md` 寫「AgentOS v1 只接受 `_type: exec` 與 `builtin`，其餘限制如下」。
   argv mapping 的 position/flag/separate/repeat、排序 tiebreak、UTF-8 clip、binary 偵測、
   limits —— 這些都已經有 45 關 roundtrip 測試在跑了，重寫一次只是把測試過的東西換成沒測試過的。
2. 如果堅持 AgentOS 要有自己的格式契約，那**至少把 `_version` 改掉**（例如 `"agentos.exec.v1"`），
   並且在文件裡明列與 tooljson 的差異表，不要讓兩份同名同版本的規格在同一個 repo 裡漂移。

順帶：`tooljson.set_approver(fn) -> bool` 是 exec 專屬的放行 hook，正好是 step mode 需要的那個接點。
現在 agent-machine 的文件完全沒提到它。

### 「因為以後要用 C++ 重寫，所以要有自己的格式契約」不成立

`MEMBERS.md` 用這個理由支持另寫一份。但 `tooljson/README.md` 開宗明義就是：

> 規範才是主體，這個 package 只是它的第一個實作。之後別的語言的 lib 讀同一份 JSON，
> 要組出一模一樣的命令列，所以那兩份 .md 裡每條規則都是死的（包括排序的 tiebreak 怎麼定）。

**tooljson 本來就是為了跨語言而寫的規範**，argv 排序 tiebreak 這種只有真的做過第二個實作才會想到的
細節都已經寫死了。所以「C++ 需要一份契約」不是分叉的理由 —— 它正是**不該**分叉的理由。

現在的狀況是：同一個 repo 裡有兩份都自稱跨語言權威、都是 `_version: "0.1.0"`、
而且在 `1.0` 該變成 `1` 還是 `1.0` 這種會直接影響「跑出哪一條命令列」的事情上**已經不一致**的規格。
等到真的動手寫 C++ 那天，第一個問題會是「我該實作哪一份」。

（tools 的長期方向見第十一節。那個方向讓「直接依賴 tooljson」這個建議更強，不是更弱。）

---

## 三、可以刪掉的複雜度（依價值排序）

### 3.1 model unload、model resource lock、direct Ollama adapter ← 最大的一筆

目前為了「`llm set model` 要先安全卸載舊模型」，設計付出了：

- 第二把 lock（model resource lock）與固定的 `worker → model → state` 三層 lock ordering
- `RUNTIME.md` 裡最長也最難的一整段 model mutation 流程（拿 lock、放 lock、重驗、再拿、再驗、才 commit）
- `$XDG_RUNTIME_DIR/agentos/models/` 的 lock 目錄、UID/mode 檢查、SHA-256 lock filename
- `model_busy`、`--no-wait`、`unload: not_supported` 這些額外的錯誤路徑
- 一個只為了查 `/api/ps` residency 而存在的 direct Ollama adapter

換回來的是什麼？而且它**本來就不可能正確** —— 文件自己也承認「無法約束其他程式直接呼叫 Ollama」。
Ollama 自己有 `keep_alive`，換模型時本來就會自己換。

更關鍵的是：**freepy 早就解決過這件事了**。`llmkit/proxy` 用 LiteLLM 把 DeepSeek 雲端、遠端 Ollama、
本機 LM Studio 統一成一個 OpenAI 相容端點，「換模型就是換一個字串」。

建議：

- v1 **只保留 OpenAI-compatible 一種 engine**（加上離線測試用的 fake）。Ollama 自己就有 `/v1`。
- 刪掉 `llm unload`、model lock、`model_busy`、lock ordering 那一整段。
- `llm set model` 退回成純粹的 config 修改：只需要 state lock，只需要「沒有 in-flight instruction、
  沒有 pending tool call」這個檢查（這部分要留，它是為了 messages 一致性，不是為了 VRAM）。
- 真的需要控制 VRAM 時，之後再加一個明擺著是 best-effort 的 `llm unload`（背後就是 `ollama stop`），
  而且**不要綁在 `set model` 上**。設定指令產生系統層副作用，違反這份設計自己在別處堅持的
  「`set` 只寫 local override，絕不動共用資源」。

失去的是「切模型時保證舊模型已釋放」。以單機個人使用來說，這個保證值不了它的價錢。

### 3.2 跨語言決定性：不是刪掉，是換一種付法

> **修訂（C++／Janet 重寫已確認一定會做）：** 我原本建議把 JCS 和「三語言一致」延後。
> 前提改變後這個建議收回一半 —— 目標要留，但**付款方式**要換。

問題不在「要不要跨語言一致」，在於現在的付法是：**在程式存在之前，用散文把 byte-exact 行為寫死。**

這有三個結構性缺陷，而且第二節的表格已經證明它們不是假想：

1. **散文規格是行為的第二份手抄本**，而手抄本一定漂移。tooljson 和 agent-machine 兩份都是人手寫的
   跨語言權威，都寫得很仔細，結果在 `1.0` 該變 `1` 還是 `1.0` 上就已經分岔了。
2. **散文無法被機器檢查。** 沒有任何 CI 能驗證「文件說的」和「Python 做的」一致。
   等 C++ 寫出來對不上時，你無法判斷是誰錯。
3. **還沒跑過的規則有相當比例是錯的。** 現在把它凍進散文，C++ 會忠實地繼承 Python 從來沒機會暴露的錯誤。

正確的付法是 **fixture corpus**，而且理由正是「Python 是 oracle」這句話本身：
如果 Python 是 oracle，那 oracle 的**輸出**就是規格，不需要人再手抄一份。

```text
散文  →  說明意圖、講清楚為什麼這樣定、列出反直覺的 case
fixtures →  凍結行為本身，(input → expected output) 全部 checked in
文件裡的契約句 →  「C++ 必須重現 fixtures/ 的每一筆」
```

fixtures 不可能跟實作漂移（用實作產生、進版控、CI 驗）、在新語言可機械驗證（跑一遍 diff）、
而且 Python 一旦能跑就幾乎免費（`python -m agent_machine._checks --dump-fixtures`）。

**這個模式 repo 裡已經有了**：tooljson 的「45 關 exec roundtrip」加上 `examples.build("/tmp/demo")`
把假工具連 spec 一起寫出來 —— 那就是 fixture generator。照抄它。

順序是：**Python 實作 → 用起來、讓格式在真實使用中穩定 → 凍成 fixtures → 才動手 port。**
不是「先凍散文 → 實作 → port」。凍結點在中間，不在最前面。

### 3.2.1 而且不是所有東西都需要一致

現在的文件對所有東西一視同仁地要求 byte-identical，這是規格膨脹的主因。實際上分三層：

| 層 | 內容 | 要求 |
|---|---|---|
| A | 落盤格式、tool result 字串、argv 組裝、stdout／exit code | **byte-identical**，進 fixtures |
| B | `status`／`log` 的人類輸出、錯誤訊息措辭 | 只要語意相容 |
| C | hash、lock 檔名、內部 instruction 編號 | **不需要一致**，只要不跨版本邊界 |

B 這一層文件其實已經做對了 —— `OUTPUT.md` 明講「`message` 給人看，可以改好懂的措辭；
程式只比較穩定、簡短的 `code`」。把同一個原則推廣到其他人類輸出即可。

C 這一層才是關鍵：**JCS 只有在 hash 會跨版本邊界時才需要。** 現在三個 hash 用途分別是 ——

- **model lock key**：會跨（Python bot 和 C++ bot 要對同一個 lock 檔名）→ 但整個 model lock 建議刪掉（3.1）。
- **tool spec hash**：instruction 本來就已經存了**解析後的完整 argv 與 executable 絕對路徑**，hash 是多餘的。
- **instruction input hash**：recovery 要回答的是「這個 result 屬於哪條 instruction」，遞增的 `seq` 就夠了。

三個都能消掉 → RFC 8785 那一整節可以不做。`JSON.md` 需要留的只有「讀取規則」，
加上 `TOOL_SPEC.md` 錯誤排序要的那一條 **key 依 UTF-16 code unit 排序** ——
那是一句話，不是整套 canonicalization。

### 3.2.2 「一定會做」真正該買的準備

比散文決定性有用得多，而且現在做很便宜、以後做很貴：

1. **每個落盤檔都要有 `format` 欄位。** 三份 raw manifest 和 event 已經有了（`agentos.llm.raw.v1`、
   `agentos.event.v1`），`state.json` 目前沒有 —— 補上。這就是 port 需要的九成：
   C++ 能不能打開 Python 建的 Bot，靠的是這個，不是 hash。
2. **想清楚 Python 與 C++ 共存期。** 一定會有一段時間兩個版本在同一台機器上。至少要定：
   flock 的檔名與語意兩邊必須相同；未知 `format` 一律 fail closed 不猜。
3. **把 policy 收斂成一個接縫 —— 這是 Janet 的準備工作。**
   `RUNTIME.md` 已經定了「Janet 只能對 immutable snapshot 回傳 typed decision」，邊界是對的。
   但現在的 v1 幾乎**沒有 policy**：limit 檢查散在 worker 裡、approval 藏在 step mode 裡、
   全都是寫死的規則。等 C++ 寫完才要找出「哪些是政策」會非常痛。
   建議現在就在 Python 裡留一個 `decide(snapshot) -> action` 的窄函數，即使它一開始只回傳常數。
   Janet port 就變成「換掉一個函數」，而不是「把散落各處的決策找出來」。
4. **純函式與 I/O 分離。** JSON 驗證／composition、argv 組裝、狀態轉移都寫成不碰 process 的純函式 ——
   那些正是 C++ 要重寫的部分，也正是 fixtures 能覆蓋的部分。
   `tooljson/args.py`（「純函式，不碰 process」）已經是這個切法的範本。

### 3.3 `$ref` 的兩個花俏功能

`$ref` 對 tools 和 llm config 是值得的（共用 endpoint 設定、共用工具集是真需求）。但這兩個可以刪：

- **`{"$ref":["a.json","b.json"]}` 的 array 合併**：deep merge 已經由 `local` override 做了一次，
  這是第二套合併機制。兩套合併規則 = 使用者要記兩次優先權。
- **`file.json#part/name` fragment**：它存在的唯一理由是「一個 ref 只能產生一項」，
  所以一個裝了三個 tools 的檔案要寫 `bundle.json#tools/2`。**要使用者數陣列 index 是很糟的 UX。**
  改成按名字選更好：

```sh
./tools add ../shared/bundle.json            # 全部加入
./tools add ../shared/bundle.json read_file  # 只加這一個
```

順帶：`$env` 找不到變數又沒有 default 時，現在是回 JSON `null`「交給 schema 判斷」。
這會讓錯誤在離現場很遠的地方爆出來。改成當場報錯並印出變數名字。

### 3.4 公開狀態 `ready`

文件自己說「`ready` 通常很短暫」。使用者幾乎看不到它，卻必須學它，而且它在 `COMMANDS.md` 的表格裡
佔一整欄，行為是「`run next` → 拒絕：先 pause」。
使用者的困惑會是：我剛剛沒在跑啊，為什麼不能 step？

建議 v1 對外把 `ready` 併入 `working`。內部 journal 照舊區分。這樣公開狀態剩 7 個，
指令表少一欄，也少一類難解釋的拒絕。

唯一的代價：crash 或 worker spawn 失敗後會顯示 `working` 但其實沒人在做事。這個 `status` 補一行
提示就好（例如 `working (no worker; run continue)`）。

---

## 四、你可能沒考慮到的：與母體願景的結構矛盾

`freepy/notes/agent-machine.md` 和 `docs/freepy/agent-machine/` 的核心結構是：

```text
bot-a           ← 單一可執行檔案，就是函數本身
.bot-a/         ← 同層的 companion directory，放長出來的狀態
```

文件明講這是為了「避免把檔案轉成目錄，讓既有呼叫路徑與檔案 identity 保持不變」，而且
`docs/freepy/agent-machine/04-python.md` 的 **P0 gate 把 `name`／`.name` 配對列為要寫 property test 的
核心不變式**。整個「OS 自己也是一個函數」的遞迴表示都建立在這個形狀上。

但 `agent-machine/` 這一套是：

```text
bot-a/          ← 目錄
  start status run llm messages tools
  .agentos/
```

**這兩個是相反的。** `./bot-a` 不再是可執行檔案，`name`/`.name` 配對不存在，遞迴表示也沒了。
文件裡沒有任何一處說明這個轉向，兩邊都還在維護中。

**已定案：目錄形式。** 補一個支持這個決定的理由 —— 單檔 + companion 那個形狀其實是
**VFS 時代的產物**：它之所以優雅，是因為在一個自製的 namespace 裡，「函數是一個 node」
可以由 lookup 規則保證配對。放到真的 Linux filesystem 上，`mv bot-a bot-b` 就會拆散配對，
而沒有 VFS 幫你維持不變式。所以捨棄 VFS 之後，目錄形式不只是妥協，是唯一還說得通的選擇。

底下這張表留著，是為了記錄放棄了什麼。

| | 目錄形式（現在） | 單檔 + companion（願景） |
|---|---|---|
| `mv` / `cp` / `rm` / git | 原子的，一個路徑搞定 | 要成對搬，rename 會拆散配對 |
| `ls bot-a/` | **直接看到 API，最好的自我說明** | 看不出有什麼能力 |
| 呼叫 | `./bot-a/start "..."` | `./bot-a "..."` |
| 檔案數 | 6 個 shim | 1 個 |
| 願景一致性 | 斷了 | 一致 |

`mv`/`cp` 的原子性和 `ls` 的自我說明性，換掉那個遞迴優雅是划算的。剩下要做的事：

1. 在 `README.md` 或 `NOTES.md` 明確記一筆「v1 選目錄形式，理由如上」，
2. 同步更新 `freepy/notes/agent-machine.md` 與 `docs/freepy/agent-machine/`，
   否則 04-python.md 的 P0 會去實作一個已經被放棄的不變式（`name`/`.name` 配對 property test）。

另外建議把 `agentos start ./bot-a "..."` 這個形式升為**一等公民**（`RUNTIME.md` 顯示它已經以
`agentos call BOT MEMBER` 的形式存在於內部）。理由：clone 下來還沒有 shim 時可用、
腳本裡不必玩 cwd、以後 `agentos status --all` 有地方放。

---

## 五、上手體驗：目前最弱的一環

現在的路徑是「讀 2000 行文件 → 自己寫 tool JSON → 才有一個會做事的 bot」。以下每一項都便宜，
而且直接決定別人（或三個月後的你）願不願意用它。

### 5.1 `agentos new` 之後應該立刻能跑

現在 `new` 產生 `{"engine":"echo"}`，使用者必須知道要設 engine、endpoint、model、api_key 四個東西。

建議：`new` 從環境變數推出可用的預設（`OPENAI_BASE_URL` / `OLLAMA_HOST` / 本機 4000 proxy），
讓 `agentos new bot-a && ./bot-a/start "hello"` 直接得到真的回答。
推不出來時，錯誤訊息要印出**確切的下一條指令**，不是描述問題。

`llmkit/llms` 已經有這個行為可以抄：`key` 沒給就吃 `OPENAI_API_KEY`，再沒有就用 `"hello"` 頂著。
`presets.json`（id → endpoint/model/parameters）更是比 `$ref` 路徑友善得多的共用設定形式：

```sh
./llm use lm-qwen3.5-9b       # 先查已安裝的 preset，查不到才當路徑
```

### 5.2 `llm check` 不連線，但使用者第一個想知道的就是「連得上嗎」

`MEMBERS.md` 明講 `check` 只驗資料、「不花 token、不偷偷 load model」。
這在概念上乾淨，但把 day-one 最重要的問題留給了使用者自己猜。

建議加 `./llm check --live`（或 `agentos doctor`）：發一次最小請求，回報端點可達、模型存在、
key 有效。花的 token 可以忽略，省下的除錯時間不行。

### 5.3 沒有 tools，bot 就不是 agent

`new` 只給 `ask_user`。所以「開箱」狀態下這個系統只能聊天，要變成 agent 得先自己寫 exec tool JSON。
**這是最大的上手斷點。**

建議：安裝包附一組唯讀的標準工具（`read_file`、`list_dir`、`grep`），用名字加入：

```sh
./tools add read_file      # 從已安裝的 catalog
agentos tools available    # 看有哪些
```

寫入類與 `run_shell` 保持 opt-in，這樣「預設安全、要威力自己開」是說得清楚的立場，
比宣稱有防護誠實。

### 5.4 一個完全不需要 LLM 的 demo bot

建議 `agentos new demo --demo`：內建一個 scripted engine（依序回放固定的回覆，含 tool calls）
加兩個假工具。這樣：

- 新使用者 30 秒內看完 `start --step` → `next` → `next` → `continue` 全流程，不必先有模型。
- 它同時就是 CLI 行為的整合測試 fixture。

投入很小，同時解決 demo 與測試兩件事。

### 5.5 「這是 agent 的 gdb」—— 這句話文件裡沒有

`start --step` / `next` / `continue` / `pause` / `status` 就是 run / step / continue / break / where。
使用者已經會了，只是不知道自己會了。在 README 寫一句類比，能省掉一半的學習曲線。

### 5.6 前景執行時要有東西在動

本機模型跑 60 秒卻整個畫面不動，是 local LLM 最糟的體驗。`OUTPUT.md` 只寫「進度走 stderr」，
沒有規定串流。

建議：前景 `start` / `run wait` 把 assistant 文字**邊生成邊寫到 stderr**，最終答案仍然只走 stdout。
`llms` 已經有串流與 tool call 的 `Accumulator`，接上即可。stdout 給程式、stderr 給人，
文字出現兩次是正常且正確的 CLI 行為。

### 5.7 錯誤訊息一律印出下一條指令

這應該寫成一條全域輸出規則，而不是每個地方各自發揮：

```text
error: 這個 Bot 已經有工作在進行
  看狀態：  ./status
  停掉它：  ./run stop
```

`paused + reason: limit` 就印 `./run more 32`；pending tool 擋住 `send` 就印
`./run skip "理由"`。目前文件裡的 `pause_first`、`busy` 這些 code 對機器夠了，對人不夠。

### 5.8 缺一個 `agentos clone`

「要平行做兩件事就再建一個 Bot」是對的答案，但沒有指令支援。
`agentos clone bot-a bot-b`（複製目錄、去掉 `.agentos/`）是三行程式，
而且它就是 `NOTES.md` 裡「繼承」的 KISS 版本 —— 先有 clone，繼承的需求可能就消失了。

---

## 六、幾個具體的洞（不大，但會咬人）

**`run wait` 在沒有 TTY 時碰到 `waiting` 會永遠卡住。** `OUTPUT.md` 說「waiting 時顯示所需動作並繼續等」。
在 CI 或 `nohup` 裡這就是掛死。建議：無 TTY 且進入 `waiting` 時印出問題並 exit 1，
要阻塞的人明確加 `--wait-for-answer`。

**history 沒有出口。** messages 屬於 Bot、`start "more"` 一直延續、`LIMITS.md` 明確不做 token 上限 ——
所以一個長期使用的 Bot 最後一定會撞到 context window，然後得到一個看不懂的 400 錯誤。
v1 不必做摘要或截斷，但至少要：`status` 顯示 history 的約略 token 數；
撞到 context 上限時的錯誤訊息直接說「history 太長，執行 `./messages clear`」。

**`messages clear` 的語意會嚇到人。** 文件說 clear 會「從 source list 移除非 system messages」——
也就是會刪掉使用者用 `messages use` 明確設定的 context refs。使用者對 clear 的預期是「忘掉對話」，
不是「刪掉我的設定」。建議：`clear` 只清 history；要動 source 一律走 `clear --all`。

**tool 的 cwd 沒有解答「Bot 放哪裡」。** cwd = Bot root，那要檢查一個專案時，Bot 得放在專案裡的子目錄，
所有相對路徑都變成 `../src/...`。文件沒有 workspace 概念，但 `CONFIG.md` 又提到「未特別指定 workspace 的
tool」，暗示有這個東西。建議明確化：Bot 有一個 workspace 設定，預設是 Bot root，
`_extra.cwd: null` 表示「用 Bot 的 workspace」。一個旋鈕、一個預設值，寫一行就講完。

**exit code 1 同時表示「狀態拒絕」和「工作失敗」。** 腳本會想分辨。`--json` 的 `error.code` 有，
但 text mode 沒有。可以考慮拒絕用 3、失敗用 1，或至少在文件裡明說要分辨就用 `--json`。

**journal 與 archive 沒有保留政策。** 每個 Bot 的 `journal.jsonl` 和 `archive/` 無限成長。
一行話的政策就夠（例如 archive 只留最近 N 次）。

---

## 七、一個很小、但值得加的安全設計

現在的安全模型是二選一：auto mode 全自動跑所有工具，或 `--step` 每一條都要按 `next`。
中間沒有東西，而中間才是日常。

建議在 tool spec 加一個 boolean：

```json
"_extra": {"_type":"exec", "confirm": true, ...}
```

auto mode 碰到 `confirm: true` 的 tool，在 dispatch 前停成 `paused + next: tool`。
使用者 `run next` 放行或 `run skip "理由"` 拒絕。

**它不需要任何新狀態、新指令或新機制** —— 完全重用既有的 pause/next/skip。
一個 boolean 就讓「讀檔自動跑、寫檔和 shell 要問我」成立，這是沒有 sandbox 的情況下
CP 值最高的一道防線。

---

## 八、文件本身：17 份 → 6 份

`DESIGN.md`、`INTERFACE.md`、`COMMANDS.md`、`SCENARIOS.md` **四份文件在描述同一組指令**，
加起來約 600 行，只是詳細程度不同。任何一次介面調整要改四個地方，它們一定會漂移。
（現在其實已經開始漂了：`DESIGN.md` 的 run 動詞表沒有 `more`，`INTERFACE.md` 的也沒有，
但 `COMMANDS.md` 和 `LIMITS.md` 有。）

建議合併成：

| 文件 | 併入 |
|---|---|
| `README.md` | quickstart（≤6 個指令）＋ 一段模型說明 ＋ 三個連結。目前的舊命令那段直接刪掉 |
| `INTERFACE.md` | ＋ DESIGN ＋ COMMANDS ＋ SCENARIOS ＋ OUTPUT ＋ LIMITS |
| `MEMBERS.md` | ＋ MESSAGES ＋ STORAGE ＋ CONFIG ＋ JSON 的讀取規則 |
| `TOOLS.md` | 取代 EXEC ＋ TOOL_SPEC ＋ JSON 的 schema 子集（理想上只寫「差異表 + 引用 tooljson」） |
| `RUNTIME.md` | ＋ RECOVERY |
| `NOTES.md` | 不動。它的定位（明講「不是契約」）是全部文件裡最健康的一份 |

還有一件事：現在**每一份文件都用契約口氣寫**，但實際上只有極少數行為被實作過。
建議在每份文件開頭放一個明確標記：

```text
狀態：已實作 / 已凍結 / 提案中（實作後可能推翻）
```

`NOTES.md` 已經這樣做了，其他 16 份沒有。

---

## 九、施工順序：建議把它倒過來

`docs/freepy/agent-machine/04-python.md` 的 P0→P3 是「先做 namespace/generation/CAS/journal/Git 三個 PR，
過關之後才接真 LLM」。理由（避免模型延遲掩蓋資料一致性問題）在工程上成立，但代價是：

**在使用者能跟模型講第一句話之前，要先蓋三個 PR 的機械。**

以 KISS 和「早點知道介面對不對」來說，我建議倒過來：

```text
第 1 步   new → llm set model → start "hi" → 印出答案
          單一 state.json + append-only journal，atomic rename（~30 行，不是負擔）
          沒有 worker、沒有背景、沒有 lock：前景同步跑完

第 2 步   tools（直接用 tooljson）+ 一個 exec tool 真的跑起來
          --step / next / skip / status

第 3 步   detached worker、flock、pause/continue/stop、crash 修復

第 4 步   $ref / $env 組合、shared config
```

理由：整個提案的價值主張（「Bot 是一個可以直接跑的目錄」）在第 1、2 步就能被驗證或推翻，
大約 300 行。而現在被寫死的那些 crash 語意（`prepared` vs `dispatching`、`outcome: unknown`、
三層 lock ordering）保護的是單人使用時幾乎不會遇到的情境，卻佔了規格 40% 的難度。
journal + atomic rename 從第一天就做（便宜、事後補很痛），其餘的修復機制留到第 3 步。

另外：Windows 上開發時，請刻意保持「純」的部分（JSON 讀取、composition、驗證、argv 組裝）
不依賴 OS，讓 `_checks` 在 Windows 原生就能跑，只有 flock/fork/exec 需要 WSL。
`docs/.../04-python.md` 的 P1 gate 已經這樣寫了，但 `agent-machine/README.md` 目前是一句
「Linux-only、一律從 WSL 執行」。這會讓你自己每天多一層摩擦。

---

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

## 十一、tools 的目標形態：檔案存取 + argv 統一介面（已確認方向）

這個方向是對的，而且它比聽起來便宜 —— 幾乎不需要新機制。但它會改變三件事的優先順序，
其中兩件是**修正我自己前面的說法**。

### 11.1 它證明了 argv mapping 是該投資的那一半

把 `_extra` 的欄位按這個目標分類，會分得很乾淨：

| | 欄位 | 地位 |
|---|---|---|
| **永久** | `exec`、`argv`（position/flag/separate/repeat）、`ok_exit`、`timeout`、`cwd` | 這就是「argv + 一個 linux 檔案」本身 |
| **過渡** | `stdin`、`stdout.clip`、`stdout.max_bytes`、`stderr.mode`、`limits` | 全都是「stdout 當結果」的產物 |

所以第二節「直接依賴 tooljson」不只是省工 —— tooljson 已經測過 45 關的那一堆
（argv 排序 tiebreak、boolean flag、repeat、型別轉字串），**正好就是永久的那一半**。
自己重寫等於把已驗證的永久部分換成未驗證的，同時繼承過渡部分。

順帶一個一致性的觀察：這個方向也讓「v1 只收 `exec` 和 `builtin`」從一個 v1 限制，
變成一個**有原則的最終決定** —— 因為如果一切都是 argv + 檔案，`exec` 本來就是唯一需要的 `_type`。
而 `builtin` 是唯一該有的例外，理由也很乾淨：`ask_user` 讀的不是檔案，是人。
（Plan 9 的講法：它是 user 這個 device 的介面。這個類比之後可能有用，但 v1 不必動它。）

### 11.2 唯一需要現在想清楚的：模型看到什麼

tool 寫了 10MB 到檔案，模型看到什麼？這是這個方向真正的設計問題，其餘都是既有機制。

```text
現在：  result = stdout，截到 65536 bytes
目標：  result = 一行摘要 + 路徑；要看內容再叫 read_file(path, offset, limit)
```

價值不只是美學，有三個實質好處：大結果不炸 context；結果持久、可以分段重讀；
而最關鍵的是 —— **tool A 的輸出可以直接餵給 tool B，完全不經過模型的 context**。
這是「檔案空間即記憶體」在實務上真正兌現的地方。

而它需要的東西少得驚人，三件都不用改格式：

1. 一個明確的 workspace 概念（見下）
2. catalog 裡有 `read_file(path, offset, limit)`（第 5.3 節本來就要做）
3. 一個約定：tool 把資料寫檔案、stdout 只印一行摘要

第 3 點是約定不是機制，所以可以現在就在自己寫的 tool 上開始遵守，不必等任何實作。

### 11.3 修正第六節：workspace 不是小旋鈕，是中心概念

第六節我把 cwd/workspace 當成「一個旋鈕、預設 Bot root、寫一行就講完」。
在這個方向下**那是錯的** —— workspace 就是 tools 之間傳遞資料的媒介，是這台機器的記憶體。

所以它要提早定，而且要明確：

- workspace 是 **Bot 的明確設定**，不是埋在 tool spec 裡的 `cwd: null` 語意。
  `_extra.cwd: null` 應該解讀成「用 Bot 的 workspace」，而不是「Bot root」。
- 每個 Run 有自己的 scratch（例如 `.agentos/run/scratch/`），隨 run 一起 archive ——
  中間產物的 provenance 就順便有了，不必另外設計。

### 11.4 修正第 10.3 節：GC 的類比重新有了指涉對象

我在 10.3 說「沒有 object graph，GC 的最終形態就是一行保留政策」。
在檔案介面下這句話要收回一半：中間檔案會**真的**累積，Lisp machine 那個 GC 類比重新有了對象。

但 KISS 的答案仍然不是 reachability 分析。是：

```text
每個 run 一個 scratch 目錄  +  archive 只留最近 N 次
```

差別在於，現在這不只是清垃圾，它同時定義了「一次工作的產物範圍」。
真的需要跨 run 保留的檔案，使用者自己寫到 workspace，不進 scratch —— 這條界線比 refs／pin 好懂太多。

### 11.5 一件要先講清楚的事：workspace 看起來像邊界，但不是

`EXEC.md` 很誠實地寫了 exec 擁有完整使用者權限、沒有 sandbox。但當 tools 全部改成透過檔案溝通、
而且有一個叫「workspace」的目錄時，**它會看起來像一個邊界**，使用者會很自然地以為工具只碰得到那裡。

兩條路：在那之前接上 `docs/research/sandboxing/` 的結論，或者在 workspace 這個詞第一次出現的地方
就把「這不是邊界」寫在旁邊。在真的有隔離之前，第七節那個 `confirm: true` 仍然是 CP 值最高的防線。

---

## 十二、還沒定案的事

1. **名字**：專案叫 Agent Machine，介面文件叫 AgentOS，binary 叫 `agentos`。挑一個。
   （順帶：對一個不管理硬體的東西叫 OS，會一直被問「kernel 在哪」。捨棄 VFS 之後更是。）
2. **tools 是依賴 `tooljson`，還是正式分叉？** 這是第二節在問的那個決定，也是現在唯一有時效性的一題 ——
   兩份規格每多活一天就多漂移一點。若選分叉，至少今天就把 `_version` 改掉。
3. **這是給誰用的？** 全部文件都在講「怎麼做」，沒有一句講「誰、為了什麼」。
   是 (a) 你自己的個人 coding agent、(b) 大量小自動化 bot 的底座、
   還是 (c) Agent Machine 願景的研究載體？
   目前的**文件**在服務 (c)，但**介面**在服務 (a)/(b)。捨棄 VFS 已經把天平推向 (a)/(b)，
   文件還沒跟上。確定這一點，上面一半的取捨會自己有答案。

---

## 附：如果只做三件事

1. 刪掉 model unload / model lock / direct Ollama adapter（第 3.1 節）。
2. tools 直接用 `tooljson`，或至少把版本號分開並列出差異表（第二節）。
3. 讓 `agentos new bot-a && ./bot-a/start "hello"` 開箱就得到真的答案，
   並附一個不需要 LLM 的 `--demo` bot（第 5.1、5.4 節）。
