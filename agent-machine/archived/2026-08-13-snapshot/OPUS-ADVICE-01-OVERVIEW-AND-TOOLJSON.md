<!-- archive-nav:start -->
上一頁：起點 · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-02-COMPLEXITY-AND-FIXTURES-A.md)
<!-- archive-nav:end -->

<!-- archive-original:start -->
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

<!-- archive-original:end -->

<!-- archive-nav:start -->
上一頁：起點 · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-02-COMPLEXITY-AND-FIXTURES-A.md)
<!-- archive-nav:end -->
