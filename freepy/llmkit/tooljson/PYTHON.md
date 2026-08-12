# `_type: "python"`：叫一個 python 物件

外殼（`_version` / `_type` / 那半份 OpenAI tool）在 [FORMAT.md](FORMAT.md)，
這份只講 `_extra` 底下屬於 `"python"` 的那幾個鍵。

```json
{
  "type": "function",
  "function": {
    "name": "shout",
    "description": "把字變大聲",
    "parameters": {
      "type": "object",
      "properties": {"text": {"type": "string"}, "times": {"type": "integer"}},
      "required": ["text"]
    }
  },
  "_extra": {
    "_version": "0.1.0",
    "_type": "python",
    "module": "mytools",
    "attr": "Shout",
    "path": "./pytools/mytools.py",
    "source": {"size": 1843, "mtime": 1754460000, "sha256": "3f9a…"}
  }
}
```

**這是一份 python 專屬的 `_type`。** 別的語言的 lib 讀到它是壞檔，這是預期的 ——
FORMAT.md 已經講過「認不認得是執行環境的性質，不是檔案的性質」。

## 鍵

| 鍵 | 必填 | 是什麼 |
|---|---|---|
| `module` | ✓ | 模組名。有 `path` 指到檔案時，這是要**給那個模組取的名字** |
| `attr` | ✓ | 模組裡的哪個物件 |
| `path` | | 模組在哪：一個 `.py` 檔或一個資料夾，**相對這份 .json** |
| `source` | | 產這份 spec 當下那個 `.py` 的指紋，給 `Spec.stale` 用 |

**`attr` 跟 `function.name` 是兩件事，不要互推。** `function.name` 是模型看到的名字，
`attr` 是實作在哪 —— 同一個 class 要能用兩個不同的名字和描述暴露兩次。

### `path`：讀取端怎麼找到你的 `.py`

**沒給 `path` 的話，讀取端做的就是 `import <module>`** —— 也就是那個 `.py` 得在
讀取端的 `sys.path` 上。對「已經裝好的套件」很合理，對「我放在某個資料夾裡的一個
檔案」很不方便：換一個 cwd 就讀不到了。

`path` 就是為了這個。給了它，.json 自己知道去哪找，不靠 `sys.path`：

| 你的情況 | 怎麼寫 | 讀取端做什麼 |
|---|---|---|
| 一個資料夾裡的 `.py` 檔 | `"path": "../weather.py"` | 直接從那個檔案載入，載進來的模組叫 `module` |
| 一整個資料夾的工具 | `"path": ".."` | 從該資料夾解析 `module`，確認來源後放到 `sys.path` 最前面再 import |
| 已經 `pip install` 的套件 | 不給 | 直接 `import module` |

相對路徑以**這份 .json 自己的位置**為中心，跟 FORMAT.md 那條通則一樣。所以擺成
這樣，整個資料夾可以搬走、進 git，從任何目錄讀都不會散：

```
~/mytools/
  weather.py
  specs/get_weather.json      ← 裡面寫 "path": "../weather.py"
```

有 `path` 時，它是**指定來源**，不是額外提供一個 import 候選。如果同一個模組名（含 dotted
name 的父 package）已經從另一個位置載進目前行程，讀取端會丟 `SpecError`，不會安靜沿用舊的
`sys.modules` 項目，也不會把它換掉。這讓同一份 spec 的 schema 與真正 effect 不會因載入順序
而分家。要切換到另一份同名實作，請用不同 module 名，或在乾淨的新行程載入。

## 參數就是 `**kwargs`

模型給的那包 dict 直接攤進去，**沒有 `argv` / `stdin` / `position` 那一整套** ——
那些是「怎麼把 dict 攤成命令列」的問題，python 這邊沒有那道關卡。

`attr` 指到 class 就**每次呼叫都建一個新實體**再叫它，指到函式就直接叫。
每次都給乾淨的實體是刻意的：兩次工具呼叫之間不留狀態，模型才不會踩到上一次的殘留。

參數對不上（多給、少給、名字錯）**回一個字串，不丟例外**，因為那是模型的錯，
那個字串會變成 tool message 讓它自己改一次。

## 回傳值怎麼變字串

body 協定要求 `run()` 回字串，但 python 函式什麼都回得出來，所以規則寫死：

| 回傳 | 變成 |
|---|---|
| `str` | 原樣 |
| `None` | `(no output)` |
| 其餘 | `json.dumps(ensure_ascii=False, default=str)` |
| 序列化不了 | `str(value)` |

**函式丟例外會被接住轉成 `Error: <型別>: <訊息>`**，一樣是給模型看的字串。
太長的輸出照 `text.py` 的 `MAX_OUTPUT` 裁掉 —— 函式回一個十萬字的 list 跟執行檔
吐一坨 log 是同一個問題。

## import 的時機

**建構時就 import**，不是等到第一次 `run()`。

所以 `load()` 一份指到不存在的模組的 .json 會**當場丟 `SpecError`**，而 exec 型的
「找不到執行檔」是 `run()` 時回字串。**兩種 `_type` 行為不一致，這是知道的**：
模組不見了是設定錯（給人看，越早炸越好），執行檔在模型叫的當下才不見比較像環境
問題（給模型看，讓它換一步走）。

`import` 會執行模組頂層的程式碼，所以讀一份來路不明的 .json 等於跑它指到的模組。
**這一層不處理那件事** —— 防線在外圍的沙盒，不在格式裡。

## `source` 和 `stale`

`source` 是產 spec 當下那個 `.py` 的指紋。`Spec.stale` 拿它跟現在的檔案比：

- `False` —— 沒變
- `True` —— 檔案改過了，schema 可能已經對不上實作
- `None` —— 不知道（沒記 `source`，或檔案不見了）

`from_tool()` 會自動記，除非那個 class 問不出檔案（REPL 裡定義的）。
**問不到就不記**，因為 `None` 的「不知道」比記一個假的指紋誠實。

## 產生端：`Tool`

.json 不用手寫，繼承 `Tool` 宣告完再 `from_tool()`。你原本的函式一個字都不用改，
只是在旁邊多寫一個宣告：

```python
# ~/mytools/weather.py
import tooljson

def get_weather(city: str, time: str) -> str:
    ...                                  # 你原本的函式

class GetWeather(tooljson.Tool):
    name = "get_weather"                 # 留空就用 class 名
    description = "查某個城市在某個時間的天氣"
    params = {"city": {"type": "string", "description": "城市名，用英文，例如 Taipei"},
              "time": {"type": "string", "description": "ISO 8601，例如 2026-08-09T14:00"}}
    required = ["city", "time"]

    def run(self, city, time) -> str:
        return get_weather(city, time)

if __name__ == "__main__":
    tooljson.save(tooljson.from_tool(GetWeather, path="../weather.py"),
                  "specs/get_weather.json")
```

`python weather.py` 跑一次，`specs/get_weather.json` 就出來了。**這一步只跑一次**，
改了宣告才要再跑。

**schema 是手寫的，不是從 signature 反射出來的。** 這是刻意的：`description` 和
`enum` 是寫給模型看的，函式簽名裡本來就沒有那些資訊 —— 反射猜得出型別，猜不出
「這個參數該怎麼用」。`params` 直接就是 JSON Schema 的 properties，不發明第二套
DSL，所以 `enum` / `minimum` / `items` 都照 JSON Schema 寫，不用等這個 lib 支援。

### `module` 是自己問出來的，`__main__` 也接得住

`from_tool()` 不給 `module=` 就從 class 自己問。這裡有一個 python 的陷阱：
**`python weather.py` 這樣直接執行時，那個檔案在 python 眼裡叫 `__main__`，不叫
`weather`** —— 那是「正在被執行的那支程式」的意思，別的行程 `import` 不到它。
照抄進 .json 就是壞檔，而且**現在存得出來，等別的程式去讀才炸**。

所以 `from_tool()` 遇到 `"__main__"` 會**從檔案位置反推**（`weather.py` → `weather`），
上面那個寫在 `if __name__ == "__main__":` 裡的例子因此是對的，不用多給參數。

**只看檔名是不夠的**：`python -m base_tools.specs` 的 `specs.py` 住在一個 package
裡，正確答案是 `base_tools.specs`，反推成 `specs` 一樣是別的行程 import 不到的名字。
所以它會一路往上收有 `__init__.py` 的資料夾，收到不是 package 為止。

反推不出來的時候（class 真的定義在一個叫 `__main__.py` 的檔裡、或在 REPL 裡）
**直接丟，不猜一個** —— 猜錯的代價是一份看起來正常、讀的時候才壞的 .json。
那種場合自己給 `module=`。

## 什麼時候**不要**用這一套

只是要在同一支程式裡用自己寫的函式 —— `llms.to_tools(fn)` 就夠了，它會反射簽名，
不用宣告，也不用存檔。

這一套的價值在**能力變成一份設定檔**：換一份 .json 就換一組能力而不用改 import
清單、可以跟 exec 型的工具混在同一個檔案、描述能脫離程式碼調整。用不到這三件事
就不划算。
