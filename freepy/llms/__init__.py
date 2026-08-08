"""llms — 包一層薄薄的殼在 openai SDK 外面，對著本機的 LiteLLM proxy 講話。

一個 LLM instance 就是一個 bot：人格（system）、記憶（history）、能力（tools）、
思考引擎（engine）。ask() 永遠回傳一個 Reply，絕不丟例外。

bot 只會說話和開口要工具 —— 工具誰去跑、跑出什麼，由你決定後餵回來。
只做這件事：不做重試、不做 logging、不做 config 檔、不做 CLI。

用法：
    from llms import LLM, Engine, Params, to_tools

    bot = LLM(engine=Engine(model="deepseek-chat", params=Params(temperature=0.2)),
              system="你是個惜字如金的助手")
    reply = bot.ask("你好")
    print(reply.text, reply.finish_reason, reply.usage)
    if not reply:
        print(reply.err)

    # 串流：同一個 Reply，疊代它就是逐字看它說話
    reply = bot.ask("寫首詩", stream=True)
    for ch in reply:
        print(ch, end="", flush=True)

    # 思考：換引擎的模型就好
    bot.engine.model = "deepseek-reasoner"
    print(bot.ask("9.11 和 9.9 哪個大？").reasoning)

    # 工具：能力掛在 bot 上，執行在外面
    schemas, dispatch = to_tools(get_weather)
    bot.tools = schemas
    reply = bot.ask("台北天氣？")
    results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in reply.calls}
    print(bot.ask(tool_results=results).text)

檔案分工：
    client.py       LLM class：人格、記憶、能力、ask()
    engine.py       Engine class：端點、模型、旋鈕、這端點做得到什麼
    reply.py        Reply class：一個回合的結果（串流與否都是它）
    usage.py        usage 物件 -> 純 dict
    toolcalls.py    tool_calls 在 raw / history / entries 之間搬
    caps.py         問 proxy 這顆模型做得到哪些事
    content.py      url、key、圖片這些雜事
    params.py       Params：只吐出有設定的呼叫參數
    func_schema.py  python 函式 -> OpenAI tool schema（配 jsontypes / docstrings）
"""

from .client import LLM
from .engine import Engine
from .func_schema import to_schema, to_schemas, to_tools
from .params import Params
from .reply import Reply

__all__ = [
    "LLM",
    "Engine",
    "Params",
    "Reply",
    "to_schema",
    "to_schemas",
    "to_tools",
]
