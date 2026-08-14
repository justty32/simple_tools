<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-03-COMPLEXITY-AND-FIXTURES-B.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-05-GAPS-DOCS-AND-BUILD-ORDER.md)
<!-- archive-nav:end -->

<!-- archive-original:start -->
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

<!-- archive-original:end -->

<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-03-COMPLEXITY-AND-FIXTURES-B.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-05-GAPS-DOCS-AND-BUILD-ORDER.md)
<!-- archive-nav:end -->
