# tooljson

一個**格式規範**，加上它的 python 標準庫。規範講的是：怎麼用一份 JSON 完整描述
「一個 tool」—— 不只是給模型看的那半邊，連「模型吐回參數之後，實際上要執行什麼」
都寫在同一份檔案裡。

- [FORMAT.md](FORMAT.md) —— 外殼，所有 `_type` 共通的部分
- [EXEC.md](EXEC.md) —— `_type: "exec"`：跑一個 linux 檔案（argv + stdin/out/err）
- [PYTHON.md](PYTHON.md) —— `_type: "python"`：叫一個 python 物件，配一個 `Tool` 基底類別

**規範才是主體，這個 package 只是它的第一個實作。** 之後別的語言的 lib 讀同一份
JSON，要組出一模一樣的命令列，所以那兩份 .md 裡每條規則都是死的（包括排序的
tiebreak 怎麼定），改文件比改程式重。

## 長什麼樣

一個原封不動的 OpenAI tool JSON，加一個 `_extra`：

```json
{
  "type": "function",
  "function": {
    "name": "resize",
    "description": "把圖片縮到指定寬度，蓋掉原檔",
    "parameters": {
      "type": "object",
      "properties": {"path": {"type": "string"}, "width": {"type": "integer"}},
      "required": ["path"]
    }
  },
  "_extra": {
    "_version": "0.1.0", "_type": "exec", "exec": ["../resize"],
    "argv": {"path": {"position": 1},
             "width": {"position": 2, "flag": "--width"}}
  }
}
```

前半直接送給模型，後半是「怎麼把它變成一次真的執行」。上面這份會組出
`../resize a.png --width 800`。

`_extra` 裡**只有 `_version` 和 `_type` 兩個保留鍵**，其餘的鍵由 `_type` 決定。
判準是：所有 `_type` 都成立的東西才配當保留鍵 —— 所以連 `timeout`、`limits`
這種看起來很通用的也不是保留鍵。

## `_type` 是開放的

內建的有 `"exec"` 和 `"python"`。**其他的你自己加**，不用改這個 package：

```python
class HttpBody:
    target = None                       # 沒有對應的本地檔案
    def __init__(self, spec):
        self.url = spec.extra["url"]    # 格式寫錯就丟 SpecError
    def run(self, args) -> str:
        return requests.post(self.url, json=args).text

tooljson.register("http", HttpBody)
```

登記完，`_type: "http"` 的 .json 就跟內建的平起平坐 —— `load()` 讀得懂、
`tools()` 收得進 dispatch、`run()` 叫得動。**內建的兩種都沒有特權**：
`exec_type.py` 和 `python_type.py` 最後一行都是 `register(...)`，同一道門。

body 只要兩樣東西：`run(args) -> str` 和 `target`（本地檔案路徑，`stale` 拿它算
指紋，沒有就 `None`）。其餘都是那個 `_type` 自己的事 —— `ExecBody` 有 `argv` /
`ok_exit` 那一堆，是因為跑執行檔需要，不是因為 body 得長那樣。細節在 `registry.py`。

## 用

```python
import tooljson
from llms import LLM

schemas, dispatch = tooljson.tools("mytools.json")     # 1. 取出給模型的定義
bot = LLM(tools=schemas)

reply = bot.ask("把 a.png 縮到 800")
results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in reply.calls}   # 2. 執行
print(bot.ask(tool_results=results).text)
```

`tools()` 收路徑或已經讀好的 `Spec`，要幾個給幾個，撞名的以先給的為準。**沒有隱含
的搜尋路徑** —— 「自動掃某個資料夾」是上面一層的事，不是這裡的事。

| | |
|---|---|
| `tools(*sources)` | → `(schemas, dispatch)`，跟 `llms.to_tools()` 同介面 |
| `load(path)` / `load_all(path)` | 一份 / 一串 `Spec`（一個檔案可以裝好幾個 tool） |
| `save(data, path)` | 寫出去，`dict` 或 `list` 都收 |
| `run(spec, args)` | 跑一次，回字串。等同 `spec.run(args)` |
| `register(kind, parser)` / `types()` | 加一種 `_type` / 看目前登記了哪些 |
| `set_approver(fn)` | `fn(name, argv) -> bool`，回 `False` 就不執行（exec 專屬） |

`Spec` 身上有 `.name`、`.schema`（剝掉 `_extra` 的乾淨 tool）、`.body`（`_type` 那邊
解析出來的東西）、`.stale`（來源檔變了沒，不知道就是 `None`）、`.run(args)`。

**送進 `LLM(tools=...)` 之前一定要剝掉 `_extra`** —— 多一個未知的鍵，
OpenAI / LiteLLM / LM Studio 三邊各有各的嫌法。`schemas` 已經是剝好的。

## 兩個回傳的規矩

**壞掉的 spec 用丟的**（`SpecError`）：那是設定錯，是人的錯，越早炸越好。
**執行的結果永遠是字串，錯誤也是字串**：那個字串會直接變成送回模型的 tool message。
模型讀到 `Error: argument 'text' is 70000 bytes, over the 65536 limit` 是能自己重試的，
讀到例外只會整條斷掉。

## 不經過 shell

argv 是一個 list，`shell=False`。模型給的參數值裡有 `;`、`$(...)`、空白都只是字元，
不會被重新解析。這是這包相對於「叫模型自己寫 shell 指令」的**實質**差別，不只是省 token。

跑的是任意執行檔，危險程度跟給模型一個 shell 同級，所以留了 `set_approver()`，
預設全放行。不做黑名單 —— 黑名單擋不住有心的組合，只會讓人誤以為安全。
真的要放生會自己動的 agent，答案仍然是容器或 VM。

守門員是 **exec 專屬**的（第二個參數就是要跑的 argv）。別的 `_type` 想要放行機制
就自己留一個，形狀由它自己決定 —— 硬湊一個通用簽章只會讓兩邊都難用。

## 驗

```bash
python -m tooljson          # 45 關，全離線，不碰 LLM 也不用 proxy
```

臨時資料夾裡放五個假工具連 spec 一起寫出來，走完整條路：讀 .json → 組 argv →
跑 → 收字串。執行的假工具是 POSIX scripts，因此完整 45 關請在 Linux／WSL 跑；Windows
原生無法直接執行這組 fixtures。涵蓋 positional / option / switch / repeat / separate、stdin、
`ok_exit`、stderr 合流、二進位輸出、截斷方向、型別轉換、limits、排序、
「值不會被 shell 重新解析」，以及最後三關的**第三方 `_type` 登記**。

那些假工具在 `examples.py`，可以直接拿來看規範實際長什麼樣：

```python
from tooljson import examples
paths = examples.build("/tmp/demo")      # 生一份出來，開 .json 看
```

## 檔案分工

| 檔案 | 負責 |
|---|---|
| `spec.py` | 外殼：讀寫 .json、兩個保留鍵、把其餘的轉交給解析器 |
| `registry.py` | `_type` → 解析器的註冊表，以及 body 要提供什麼 |
| `exec_type.py` | 內建的 `_type: "exec"` 解析器 |
| `args.py` | 模型給的 JSON → argv + stdin，純函式，不碰 process |
| `invoke.py` | exec 的執行端：真的去跑 → 一個字串；守門員 hook |
| `examples.py` | 會動的假工具和假 spec，煙霧測試和讀者都用它 |
| `__main__.py` | 離線煙霧測試 |

一個檔一件事，程式和文件都在 150 行以內。多一個 `_type` 就是多一個跟
`exec_type.py` 平行的檔案，外殼一行都不用動 —— 那是這個切法唯一要證明的事。
