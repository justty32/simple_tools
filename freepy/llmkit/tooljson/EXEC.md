# `_type: "exec"`

跑一個 linux 檔案：argv 進去，stdin/stdout/stderr 出來。外殼的規則在
[FORMAT.md](FORMAT.md)，這份只講 `_extra` 底下屬於 `exec` 的那些鍵。

```json
"_extra": {
  "_version": "0.1.0",
  "_type": "exec",

  "exec": ["../resize"],
  "argv": {
    "path":  {"position": 1},
    "width": {"position": 2, "flag": "--width"},
    "force": {"position": 3, "flag": "--force"},
    "tag":   {"position": 4, "flag": "--tag", "repeat": true}
  },
  "stdin":  null,
  "stdout": {"clip": "head"},
  "stderr": {"mode": "merge"},
  "ok_exit": [0],
  "cwd": null,
  "timeout": 60,
  "limits": {"path": {"max_bytes": 4096}},
  "source": {"size": 1843, "mtime": 1754460000, "sha256": "3f9a…"}
}
```

## exec

要跑的東西。**list 的第一項是程式，後面是固定參數**，所以 `["git", "log"]` 這種
「工具其實是某個子指令」直接就成立。

只有 `exec[0]` 是路徑，而且它照 `execvp` 的老規矩看斜線：

| 寫法 | 判準 | 解讀 |
|---|---|---|
| `/usr/bin/resize` | 開頭是 `/` | 絕對路徑 |
| `../resize`、`bin/resize` | **含有 `/`** | 相對於這份 .json |
| `resize` | **不含 `/`** | 去 `$PATH` 找，`$PATH` 繼承呼叫端 |

spec 常被集中放在一個子資料夾裡（`specs/`、`.specs/` 之類），**這時 `exec` 是
`"../resize"` 而不是 `"./resize"`** —— 中心是 .json 自己，不是工具所在的目錄。
這是實作時真的踩到的第一個坑。`exec[1:]` 是參數不是路徑，不翻。

## argv

key 是參數名，要對得上 `function.parameters.properties` 裡的同名參數（綁一個不存在
的參數是壞檔）。

**順序不看 key 的順序** —— JSON 標準明講 object 無序，有些語言的 lib 讀進來會照字母
重排，那會安靜地組出另一條命令列。看 `position`：**由小到大，同號的照參數名的
Unicode 碼位排**，沒寫當 `0`。

命令列上長什麼樣是**推導**出來的，沒有 `kind` 這種欄位 —— schema 的型別是唯一真相，
同一件事不講第二遍就不會講不一致：

| 有沒有 `flag` | 型別 | 產生 |
|---|---|---|
| 沒有 | 任何 | `值` |
| 有 | 非 boolean | `--width` `800`（兩項） |
| 有 | **boolean** | 真值才放 `--force`，不帶值；假值什麼都不放 |

每個參數的綁定可以有這四格，其餘的鍵一律是壞檔：

| | 預設 | 意思 |
|---|---|---|
| `position` | `0` | 排序用 |
| `flag` | 無 | 旗標，沒有就是位置參數 |
| `separate` | `true` | 改成 `false` 產生 `--width=800` 一項 |
| `repeat` | `false` | `true` 才收 array，每個值各展開一次 |

- **參數缺席就整條跳過**，不產生空字串。模型明確送 `null` 等同沒給。
- 被 `stdin` 指名的參數不進 argv。
- `repeat` 不是 `true` 卻收到 array → 回錯誤給模型，不猜。

值怎麼變字串，照 JSON 的字面寫法（這是跨語言最容易分岔的地方，所以寫死）：

| JSON | argv 上 |
|---|---|
| `"a b"` | `a b`（一項，不切、不加引號） |
| `800` | `800` |
| `1.5` | `1.5` |
| `true` / `false` | `true` / `false`（只有沒 flag 時才會真的印出來） |

**不經過 shell。**argv 是一個 list 直接 exec，所以值裡面有 `;`、`$(...)`、空白都
只是字元，不會被重新解析。

## stdin / stdout / stderr

`"stdin": {"param": "text"}` 表示 **`text` 這個參數不進 argv，改寫進標準輸入**
（原樣寫入，UTF-8）。`null` 就是這工具不吃 stdin。

會需要它是因為 argv 有硬上限（linux 單項 128KB，超過就 `E2BIG`），而且很多 filter
類的工具根本只肯讀 stdin。

> **沒宣告 stdin 時，子行程拿到的是一個立刻關閉的空 stdin，不是呼叫端的 stdin。**
> 這條一定要照做：讓它繼承的話，一個會讀 stdin 的工具會安靜地卡到 timeout。

| 鍵 | 值 | 意思 |
|---|---|---|
| `stdout.clip` | `"head"`（預設） | 太長時留開頭 |
| | `"tail"` | 留結尾 —— 編譯器那種重點在最後一行的用這個 |
| `stderr.mode` | `"merge"`（預設） | **真的把 stderr 導進 stdout 那條管子**，時序才是實際發生的順序，不是事後把兩段字串接起來 |
| | `"ignore"` | 丟掉 —— 有些工具把進度條寫 stderr |
| | `"only"` | 只要 stderr |

輸出含 NUL byte 就當**二進位**，只回一句 `(binary output, N bytes, not shown)`，
不要吐幾萬個替代字元灌爆 context。

## ok_exit

**哪些結束碼算成功**，預設 `[0]`。非 0 不一定是失敗：`grep` 沒找到是 1、`diff` 有
差異是 1、`test` 更是。不宣告的話模型會以為工具壞了，然後開始修一個沒壞的東西。

- 在 `ok_exit` 裡 → 直接回輸出（沒輸出就回 `(no output, exit N)`）。
- 不在 → 最前面加一行 `exit N`，再接輸出。

## cwd / timeout / limits / source

`cwd` 是工具跑起來時的工作目錄，`null` 就**繼承呼叫端** —— 模型傳的相對路徑全靠它
解讀，所以不預設成 .json 所在的位置（那是「工具放哪」，跟「要處理哪個檔」無關）。

`timeout` 是最多等幾秒，預設 60。一個卡住的工具會卡住整個 agent 迴圈。

`limits` 是參數的大小和範圍限制，超標就不跑、直接回一句錯誤給模型：

```json
"limits": {"text": {"max_bytes": 65536}, "width": {"min": 1, "max": 8192}}
```

會需要它是因為 OpenAI 的 strict schema 子集把 `maxLength`、`minimum` 這些**全砍了**，
寫了模型也只當提示。`max_bytes` 算的是 **UTF-8 編碼後的 bytes**，因為 argv 和 stdin
實際在數的就是 bytes。另外不管有沒有宣告，單一 argv 項目超過 128KB 一律擋。

`source` 是產 spec 當下那個執行檔的指紋，用來判斷 spec 過期沒（`True` / `False` /
`None` 不知道）。**過期不自動重產**，重產要花 LLM，那是上層的決定。

以上 recipe 的型別與引用關係都在載入時檢查。像 `stdout` 寫成字串、`argv` 綁到不存在
的參數、`timeout` 非正有限數字、`limits` 引用未知參數或 `min > max`，都是設定錯，
直接丟 `SpecError`；不能等模型第一次呼叫時才變成 `AttributeError` 或 process 錯誤。
