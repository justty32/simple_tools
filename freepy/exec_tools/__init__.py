"""exec_tools — 把外部可執行檔變成 LLM 叫得動的 tool。

`base_tools` 做的是「python 函式 → tool schema」，這包做同一件事、換一個來源：
**一份 JSON → tool schema + 怎麼真的去跑它**。所以對外介面刻意長一樣：

    import exec_tools
    from llms import LLM

    schemas, dispatch = exec_tools.tools()     # 讀 $FREEPY_TOOLS 底下的 .specs/*.json
    bot = LLM(tools=schemas)

存在的理由只有一個：**模型不知道你自己寫的那些工具怎麼用**。`ls` / `grep` 它看過幾百
萬次，丟給 run_shell 就好；`~/tools/fetch-invoice` 這種只有你有的東西，得有人告訴它
吃什麼參數、吐什麼。所以分工是：一般指令走 shell，特定工具走 spec。

一份 spec 就是一個 OpenAI tool JSON，外加一個 `_extra` 講怎麼把它變成實際的執行。
`_extra` 裡只有 `_version` 和 `_type` 兩個保留鍵，其餘的鍵由 `_type` 決定 ——
目前只有 `"exec"`（linux 檔案呼叫：argv + stdin/out/err），之後的 python import、
http 各自是一個平行的解析器。

格式規範見 FORMAT.md（外殼）和 EXEC.md（`_type: "exec"`）。**那是給別的語言的 lib
也讀得懂的契約**，不是這個實作的內部細節，所以改那兩份文件比改這裡的程式重。
"""

from .discover import scan, spec_path
from .invoke import bind, run, set_approver
from .spec import Spec, SpecError, load, load_all, save

__all__ = [
    "Spec",
    "SpecError",
    "bind",
    "load",
    "load_all",
    "run",
    "save",
    "scan",
    "set_approver",
    "spec_path",
    "tools",
]


def tools(specs=None):
    """回 (schemas, dispatch)，跟 base_tools.tools() 同介面，直接給 LLM(tools=...)。

    specs 不給就去掃 $FREEPY_TOOLS。schemas 已經剝掉 `_extra`，是乾淨的 OpenAI tool。
    """
    if specs is None:
        specs, _missing, _errors = scan()
    schemas = [one.schema for one in specs.values()]
    dispatch = {name: bind(one) for name, one in specs.items()}
    return schemas, dispatch
