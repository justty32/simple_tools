"""agentloop — 讓一個 bot 一直跑到它不再叫工具為止。

四層裡的第四層（見 [../NOTES.md](../NOTES.md)）。底下三層讓模型會講話、會開口要
工具，但每一步都是人在推；這一層負責**真的去執行、把結果餵回去、決定什麼時候收手**。

只有一個阻塞函式和一個把手：

    import agentloop, base_tools
    from llms import LLM

    schemas, dispatch = base_tools.tools()
    bot = LLM(tools=schemas)

    h = agentloop.run(bot, dispatch, "把 a.txt 裡的 two 改成 TWO")
    print(h.text, h.stop)

`run()` 直接等到 Round 結束。它不建 task、不開 thread，也沒有 async 版本；
需要背景執行時由上層 runtime 包裝。

把手可以改下一步：`add_instruction()`、`add_images()`、`add_tools()`、
`set_ask_options()`；也能 `pause()` / `resume()` / `ask_stop()`。都是下一步開頭
生效，不會打斷正在跑的那一步 —— 模型那次 HTTP
和跑到一半的工具本來就停不下來，假裝停得下來只會讓人以為工具沒跑過。

放它自己跑之前先給預算，`Limits` 見 [limits.py](limits.py)：

    agentloop.run(bot, dispatch, "...", limits=agentloop.Limits(
        steps=20, calls=40, per_tool={"run_shell": 5},
        tools=["read_file", "run_shell"], engines=["deepseek-chat"],
        seconds=300, tokens=200_000))

**這一包不 import llms，也不 import tooljson。** `bot` 只要有 `ask()` 和
`pending_calls`，`dispatch` 只要是 `{名字: fn(**kwargs)}` —— `base_tools.tools()`、
`tooljson.tools()`、`llms.to_tools()` 生出來的都是這個形狀，混著給也可以。

檔案分工：
    loop.py     `run()`：迴圈本身 —— 推幾步、什麼時候收手
    calling.py  一個 tool call → 一個字串（壞掉的四種也是字串）
    handle.py   `Handle`：外面問狀況、下指令的窗口
    limits.py   `Limits`：這次最多能花掉多少
    LIMITS.md   作業系統那一層的資源限制（cpu / 記憶體 / 網路 / 檔案），**還沒做**
    __main__.py 煙霧測試（假 bot，離線）
"""

from .calling import MAX_OUTPUT, perform
from .handle import Handle
from .limits import Limits
from .loop import run

__all__ = ["Handle", "Limits", "MAX_OUTPUT", "perform", "run"]
