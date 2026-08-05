"""llms — 包一層薄薄的殼在 openai SDK 外面，對著本機的 LiteLLM proxy 講話。

一個 LLM instance 就是一段對話：預設會記住歷史，呼叫 ask() 不用自己組 messages。
只做這個 class 需要的事，不做重試、不做 logging、不做 config 檔、不做 CLI。

用法：
    from llms import LLM, Params

    bot = LLM(model="deepseek-chat", params=Params(temperature=0.2, max_tokens=200))
    reply, err = bot.ask("你好")

    if bot.supports_vision:          # True / False / None（不知道）
        bot.ask("看圖", images=["x.png"])
    if bot.supports_tools is False:  # 明確不支援才會擋，None 一律放行
        ...

    # 思考模型：思考不會混進答案裡
    reply, err = bot.ask("9.11 和 9.9 哪個大？", model="deepseek-reasoner")
    print(bot.last_reasoning)

    # 串流：疊代吐答案的字，思考和 tool_calls 收在旁邊
    handler, err = bot.ask("寫首詩", stream=True)
    for ch in handler:
        print(ch, end="", flush=True)

    # 工具：schema 從函式本體生出來
    from llms import to_tools
    schemas, dispatch = to_tools(get_weather)
    calls, err = bot.ask("台北天氣？", tools=schemas)
    results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in calls}
    reply, err = bot.ask(tool_results=results)

檔案分工：
    client.py       LLM class，對話狀態和 ask()
    stream.py       串流處理器（答案 / 思考 / tool_calls）
    toolcalls.py    tool_calls 的組裝與拆解
    caps.py         問 proxy 模型支不支援 tool / vision / 思考
    content.py      url、key、圖片這些雜事
    params.py       API 呼叫參數
    func_schema.py  python 函式 → OpenAI tool schema（配 jsontypes / docstrings）
"""

from .client import LLM
from .func_schema import to_schema, to_schemas, to_tools
from .params import Params
from .stream import StreamHandler

__all__ = [
    "LLM",
    "Params",
    "StreamHandler",
    "to_schema",
    "to_schemas",
    "to_tools",
]
