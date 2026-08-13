"""tooljson — 讀一份符合規範的 tool JSON，取出要給 LLM 的定義，並且真的執行它。

規範是一個 OpenAI tool JSON 加一個 `_extra`：前半原封不動送給模型，後半講怎麼把
模型吐回來的那包參數變成一次實際的執行。這個 package 就是它的標準庫，只做兩件事：

    import tooljson
    from llms import Bot, LLM

    bot = Bot(LLM(), tools=tooljson.tools("mytools.json"))  # 1. 取出定義

    reply = bot.ask("把 a.png 縮到 800")
    results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in reply.calls}  # 2. 執行
    print(bot.ask(tool_results=results).text)

`_extra` 裡只有 `_version` 和 `_type` 兩個保留鍵，其餘的鍵由 `_type` 決定。
內建的 `_type` 有兩種：`"exec"`（跑一個 linux 檔案：argv + stdin/out/err）和
`"python"`（叫一個 python 物件）。後者配一個 `Tool` 基底類別，自己宣告 schema、
實作 `run()`，再存成 .json：

    class Shout(tooljson.Tool):
        name, description = "shout", "把字變大聲"
        params = {"text": {"type": "string"}}
        required = ["text"]
        def run(self, text) -> str: return text.upper()

    tooljson.save(tooljson.from_tool(Shout, path="../mytools.py"), "specs/shout.json")

**schema 是手寫的不是反射出來的** —— description 和 enum 是寫給模型看的，簽名裡
沒有那些資訊。只是要在同一支程式裡用自己的函式的話，`llms.to_tools(fn)` 就夠了，
不需要這一套；這裡的價值在「能力變成一份設定檔」。

**其他的 `_type` 由你自己登記**：

    tooljson.register("http", MyHttpBody)     # 見 registry.py

登記完，`_type: "http"` 的 .json 就跟內建的平起平坐 —— 讀得懂、進得了 dispatch、
叫得動，llmkit 一行都不用改。內建的 `exec` 走的是同一道門，沒有特權。

**規範才是主體，這包只是它的第一個實作。** 別的語言的 lib 讀同一份 JSON 要做出
一模一樣的事，所以改 FORMAT.md（外殼）和 EXEC.md（`_type: "exec"`）比改這裡的
程式重 —— 那兩份是契約，這裡是契約的 python 版。

檔案分工：
    spec.py         外殼：讀寫 .json、兩個保留鍵、把其餘的轉交給解析器
    registry.py     `_type` → 解析器的註冊表，以及 body 要提供什麼
    exec_type.py    內建的 `_type: "exec"` 解析器
    args.py         模型給的 JSON → argv + stdin（純函式）
    invoke.py       exec 的執行端：真的去跑 → 一個字串；守門員 hook
    python_type.py  內建的 `_type: "python"` 解析器（解析和執行都在裡面，它夠小）
    tool.py         `Tool` 基底類別 ＋ `from_tool()`：宣告一個工具、存成 .json
    text.py         每種 `_type` 共用的收尾：解碼和裁切
    __main__.py     離線煙霧測試
"""

from . import exec_type as _exec_type  # noqa: F401  —— import 就是登記，別拿掉
from . import python_type as _python_type  # noqa: F401  —— 同上，內建的兩種都走這道門
from .invoke import bind, set_approver
from .registry import register, types
from .spec import Spec, SpecError, load, load_all, run, save
from .tool import Tool, from_tool

__all__ = [
    "Spec",
    "SpecError",
    "Tool",
    "bind",
    "from_tool",
    "load",
    "load_all",
    "register",
    "run",
    "save",
    "set_approver",
    "tools",
    "types",
]


def tools(*sources):
    """回 (schemas, dispatch)，跟 `llms.to_tools()` 同介面，可給 `Bot(tools=...)`。

    sources 是 .json 的路徑或已經讀好的 Spec，混著給也可以。撞名的以先給的為準
    （比照 PATH），因為參數的順序是呼叫端明確寫下的優先序。

    schemas 已經剝掉 `_extra`，是乾淨的 OpenAI tool。dispatch 的每個值都是
    `fn(**kwargs) -> str`，**錯誤也是字串**，因為那個字串會直接變成 tool message。
    """
    found = {}
    for one in sources:
        for spec in ([one] if isinstance(one, Spec) else load_all(one)):
            found.setdefault(spec.name, spec)
    schemas = [one.schema for one in found.values()]
    dispatch = {name: bind(one) for name, one in found.items()}
    return schemas, dispatch
