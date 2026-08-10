# exec_tools 規劃

> **一半已經落地到 [`../llmkit/tooljson/`](../llmkit/tooljson/README.md)。**
> 格式（FORMAT.md / EXEC.md）和標準庫（讀 .json、組 argv、執行）都在那邊，
> 離線 24 關全過。
>
> **這裡只剩沒成型的兩件**：`discover.py`（自動掃一個資料夾找工具，還在這個資料夾裡）
> 和 `describe.py`（讓 LLM 讀腳本產 spec，還沒開始）。落地的那份**刻意沒有隱含的
> 搜尋路徑** —— 要讀哪幾份 .json 是呼叫端明講的，掃描是上面一層的事。
>
> 格式跟下面「核心資料結構」那節寫的**不一樣**了，以落地的那兩份 .md 為準。差異：
> 攤平的 `{name, exec, ...}` 改成**真正的 OpenAI tool JSON 加一個 `_extra`**（剝掉
> `_extra` 就能直接餵 `LLM(tools=...)`）；`_extra` 只有 `_version` / `_type` 兩個保留鍵，
> 其餘由 `_type` 解析；`argv` 是 object 加 `position` 而不是 array；
> 一個 .json 可以裝好幾個 tool。

把**外部可執行檔**變成 LLM 可以叫的 tool。

`base_tools` 做的是「python 函式 → tool schema」，這一包做的是同一件事、換一個來源：
「一個資料夾裡的可執行檔 → tool schema」。對外的介面刻意長一樣：

```python
schemas, dispatch = exec_tools.tools()
```

出處是 `ai_core/sub_projs/handy/notes/2026-08-06.md` 的 77–83、103–107 行。

## 範圍

那份 note 把 LLM 的用法分成四層：純工具 → 複製人 → 員工 → 團隊。

**這一包只做「純工具」，設計時最遠看到「複製人」。** 也就是說：產出的東西要能被
複製人那份 `.json` 直接引用（「我可以用哪些 tool」），但不處理員工層的東西 ——
沒有 linux user、沒有常駐程式、沒有 inbox 輪詢、沒有沙盒。那些之後再說。

note 第 56 行說純工具的「執行期屬性會被其他東西記錄與管理」—— 那個「其他東西」
就是複製人，不在這裡做。這裡不記錄任何一次執行的歷史。

## 為什麼不是直接用 run_shell

`base_tools.run_shell` 已經可以跑任何指令了，這包存在的理由只有一個：
**模型不知道你自己寫的那些工具怎麼用**。

`ls` / `cat` / `grep` 模型看過幾百萬次，丟給 `run_shell` 就好，不需要 schema。
但 `~/tools/resize`、`~/tools/fetch-invoice` 這種只有你有的東西，模型沒看過，
必須有人告訴它「這個工具吃什麼參數、吐什麼」。

所以分工是：**一般指令走 shell，特定工具走 schema**。這包只管後者。

## schema 從哪裡來

**由 LLM 讀那個檔案、自己產出 tool definition JSON。** 不寫 parser。

理由是 note 第 81 行那句：既然工具本身就是文字腳本，資訊本來就記錄在程式裡，
那就讓模型直接讀，不要再發明一套檔頭註解格式、逼每個工具遵守。

- **文字檔（腳本）**：把整個檔案內容丟給 LLM，要它產出 JSON。
- **二進位執行檔**：讀不了，**問使用者**。（`--help` 的輸出可以當提示丟給使用者參考，
  但不自動採信 —— 太多程式的 `--help` 跟實際行為對不上。）

這一步**慢、要花 LLM、而且會出錯**，所以它是**一次性的**：產出的 JSON 存下來，
之後每次跑工具都是零 LLM。人也可以直接手改那份 JSON，它是給人看得懂的格式。

> 這步本身之後會被固化成一個純工具（「產 schema 的工具」）—— 正好是 note 第 72–75 行
> 講的飛輪：先用高級 agent 跑一輪，記錄下來，再拆成寫死的程式。這次先跑第一輪。

## 核心資料結構：spec

只有 OpenAI schema 是不夠的。schema 說得出「有一個參數叫 `width`，是整數」，
但說不出「它在命令列上要寫成 `--width 800` 還是 `-w 800` 還是第二個位置參數」。

所以每個工具的 JSON 除了 schema 之外，還要有一段**呼叫慣例**：

```json
{
  "name": "resize",
  "description": "把圖片縮到指定寬度，蓋掉原檔",
  "exec": "/home/x/tools/resize",
  "parameters": {
    "type": "object",
    "properties": {
      "path":  {"type": "string",  "description": "要處理的圖片路徑"},
      "width": {"type": "integer", "description": "目標寬度，像素"}
    },
    "required": ["path"]
  },
  "argv": [
    {"param": "path",  "kind": "positional"},
    {"param": "width", "kind": "option", "flag": "--width"}
  ],
  "stdin": null,
  "source": {"size": 1843, "mtime": 1754460000, "sha256": "3f9a…"}
}
```

`argv` 的每一項把一個參數映射到命令列上的位置。`kind` 三種就夠：

| kind | 產生的東西 | 用在 |
|---|---|---|
| `positional` | `值` | `resize a.png` |
| `option` | `--width 800` | 大部分具名參數 |
| `switch` | 有就 `--force`，沒有就不放 | boolean |

`stdin` 指名哪個參數要從標準輸入餵進去（`null` 表示不吃 stdin）。
`source` 是產 schema 當下那個檔案的指紋，用來判斷 spec 有沒有過期。

**spec 過期時不自動重產**（重產要花 LLM），只在 `tools()` 回傳時標記出來，
讓上層決定要不要重來。

## 四件事

| 動作 | 做什麼 |
|---|---|
| **discover** | 從環境變數 `FREEPY_TOOLS` 指定的資料夾（`os.pathsep` 分隔，比照 PATH）掃出可執行檔 |
| **describe** | 拿到某個檔的 spec：先查快取，沒有才產 —— 文字檔丟 LLM、二進位問使用者 |
| **invoke** | spec + 模型給的 args → 組 argv、餵 stdin → 跑 → 收成一個字串 |
| **tools()** | 回 `(schemas, dispatch)`，跟 `base_tools.tools()` 同介面 |

只認 `FREEPY_TOOLS`、不碰 `PATH`：`PATH` 上有幾百個東西，全部產 schema 是災難，
而且模型的 context 塞不下。要成為 tool 就得被明確放進來，這個門檻是故意的。

### 快取放哪

**跟工具放同一個目錄底下的 `.specs/<name>.json`。**

好處是工具跟它的描述一起搬、一起進 git、一個資料夾自我完備 ——
複製人那份 `.json` 只要指到工具目錄就好，不用另外再指一個快取路徑。

（另一個選項是集中放 `~/.freepy/specs/`，工具目錄唯讀時才需要。先不做，
真的撞到再加一個 `FREEPY_SPECS` 蓋掉。）

### invoke 回傳什麼

比照 `base_tools`：**永遠回一個字串，錯誤也是字串，不丟例外**。
stdout 和 stderr 併在一起，非 0 的 exit code 在前面加一行 `exit N` ——
跟 `run_shell` 完全一樣的形狀，模型不用學第二套。

輸出上限直接沿用 `base_tools.paths` 的 `MAX_OUTPUT` / `MAX_LINE`，不重寫一份。

### 安全

跑一個外部執行檔的危險程度跟 `run_shell` 同級，所以用同一招：
一個 approver hook，預設全放行，要人工放行就自己接。不做黑名單
（理由同 `base_tools/README.md`：黑名單擋不住有心的組合，只會讓人誤以為安全）。

真的要放生會自己動的 agent，答案仍然是容器或 VM。

## 檔案分工

照這個 repo 的規矩：一個檔一件事，程式和文件都在 150 行以內。

| 檔案 | 負責 |
|---|---|
| `discover.py` | `FREEPY_TOOLS` → 可執行檔清單 |
| `spec.py` | spec 的資料結構、讀寫 `.specs/*.json`、指紋比對 |
| `describe.py` | 產 spec：文字檔 → LLM；二進位 → 問使用者 |
| `invoke.py` | spec + args → argv/stdin → 跑 → 字串；approver hook |
| `__init__.py` | `tools()` 對外介面 |
| `__main__.py` | 煙霧測試 |
| `README.md` | 寫完再補（PLAN.md 到時候刪掉） |

## 做的順序

1. **`spec.py`** —— 先把資料結構定死，後面三個檔都靠它。
2. **`invoke.py`** —— 手寫一份 spec 就能測，不需要 LLM。這步做完，
   「有 spec 就能跑工具」這條路已經通了。
3. **`discover.py`** —— 最單純的一個。
4. **`__main__.py` 離線關卡** —— 臨時資料夾放幾個假工具腳本，測掃描、argv 組裝、
   stdin、exit code、輸出截斷。到這裡整包不碰 LLM 就能驗證。
5. **`describe.py`** —— 最後做，因為它最不確定：prompt 要試幾輪才會穩。
6. **`__main__.py` 帶模型的關卡** —— `python -m exec_tools deepseek-chat`，
   真的產一份 spec 再拿去跑，比照 `base_tools` 的兩段式煙霧測試。

前四步不需要模型，可以一路寫到底；第五步開始才會有反覆。

## 先不做

- **不做 spec 的自動重產**：過期只標記，重產是上層的決定。
- **不做工具的版本管理 / 相依宣告**：這是複製人那層的事。
- **不做執行歷史、計時、成本統計**：note 說純工具的執行期屬性由別人管，就別人管。
- **不做 `--help` 自動解析**：太不可靠，二進位檔一律問使用者。
- **不碰 `PATH`**：一般指令走 `run_shell`。
