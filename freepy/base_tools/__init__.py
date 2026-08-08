"""base_tools — 給 LLM 用的四個基本工具：讀檔、寫檔、改檔、跑指令。

配 llms 用，一行就接起來：

    import base_tools
    from llms import LLM

    base_tools.set_root("/tmp/workspace")     # 模型只能在這底下動手腳
    schemas, dispatch = base_tools.tools()    # schema + name -> function 對照表

    bot = LLM(tools=schemas)
    reply = bot.ask("看一下這個資料夾裡有什麼")
    while reply.calls:                        # 模型要叫工具就照做，然後把結果送回去
        results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in reply.calls}
        reply = bot.ask(tool_results=results)

**這裡的函式回傳的永遠是一個字串，錯誤也是字串，不丟例外也不回 Reply。**
跟 llms 的慣例不同是故意的：工具的回傳值會直接變成送回模型的 tool message，
所以錯誤訊息本身就是要給模型讀的東西 —— 它看到 "Error: file not found" 會自己去找
正確的路徑，看到一個 tuple 只會困惑。訊息寫成英文也是同一個理由：模型看過的
工具輸出九成長這樣。

工具說明（送給模型的 description）則是中文，因為那是從 docstring 第一行抓的，
這個 repo 的 docstring 本來就是中文，deepseek / qwen / gemma 讀得懂。

檔案分工：
    paths.py    工作根目錄、路徑檢查、輸出截斷
    files.py    read_file / write_file
    edits.py    edit_file
    shell.py    run_shell 和它的守門員 hook
"""

from .edits import edit_file
from .files import read_file, write_file
from .paths import get_root, set_root
from .shell import run_shell, set_approver

#: 四個工具本體，順序就是給模型看的順序
ALL = (read_file, write_file, edit_file, run_shell)


def tools():
    """回傳 (schemas, dispatch)，schemas 直接給 llms 的 LLM(tools=...)。需要有 llms 這個 package。"""
    from llms import to_tools
    return to_tools(*ALL)


__all__ = [
    "ALL",
    "edit_file",
    "get_root",
    "read_file",
    "run_shell",
    "set_approver",
    "set_root",
    "tools",
    "write_file",
]
