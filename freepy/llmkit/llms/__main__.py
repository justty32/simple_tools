"""__main__.py — 拿真的 proxy 跑一遍，確認記憶、串流、思考、工具、後設都還活著。

    cd freepy/llmkit
    ../proxy/start_litellm.sh &          # 這五關**要**連得到端點，不是離線測試
    uv run python -m llms                # 預設 deepseek-chat / deepseek-reasoner
    uv run python -m llms lm-gemma-4-e4b # 換一般模型；第二個參數換思考模型

離線就驗得動的那套在隔壁：`python -m tooljson`。
"""

import sys
import typing

from .client import Bot
from .engine import LLM
from .func_schema import to_tools

URL = "http://localhost:4000"


def get_weather(city: str, unit: typing.Literal["celsius", "fahrenheit"] = "celsius"):
    """查詢指定城市的天氣。

    Args:
        city: 城市名稱
        unit: 溫度單位
    """
    return f"{city} 晴，26 度（{unit}）"


def bot_with(model, **kw):
    return Bot(LLM(url=URL, model=model), **kw)


def demo_memory(model):
    print("== 對話記憶 ==")
    bot = bot_with(model)
    print("第一句:", bot.ask("我的名字是小明，請記住。").text)
    reply = bot.ask("我的名字是什麼？")
    print("回答:", reply.text, "err:", reply.err)


def demo_stream(model):
    print("\n== 串流 ==")
    reply = bot_with(model).ask("用一句話介紹台北。", stream=True)
    for ch in reply:
        print(ch, end="", flush=True)
    print("\nerr:", reply.err)


def demo_thinking(model):
    print("\n== 思考（非串流）==")
    reply = bot_with(model).ask("9.11 和 9.9 哪個大？只回答數字。")
    print("答案:", reply.text, "err:", reply.err)
    thought = reply.reasoning
    print("思考:", (thought[:120] + "…") if thought else None)


def demo_tools(model):
    print("\n== 工具 ==")
    schemas, dispatch = to_tools(get_weather)
    bot = bot_with(model, tools=schemas)
    if bot.llm.supports("tools") is False:
        print("這個模型宣告不支援 tool calling，跳過")
        return
    reply = bot.ask("台北天氣如何？")
    print("說了:", reply.text, "| calls:", reply.calls, "| err:", reply.err)
    if not reply.calls:
        return
    results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in reply.calls}
    print("回答:", bot.ask(tool_results=results).text)


def demo_meta(model):
    print("\n== 後設 ==")
    bot = bot_with(model)
    print("能力:", bot.llm.caps)
    reply = bot.ask("從 1 數到 50。")
    print("finish_reason:", reply.finish_reason, "usage:", reply.usage)
    stream = bot.ask("再數一次。", stream=True)
    stream.text  # 收完才有 finish_reason / usage
    print("串流 finish_reason:", stream.finish_reason, "usage:", stream.usage)


def main():
    model = sys.argv[1] if len(sys.argv) > 1 else "deepseek-chat"
    reasoner = sys.argv[2] if len(sys.argv) > 2 else "deepseek-reasoner"
    print(f"proxy: {URL}  model: {model}")
    demo_memory(model)
    demo_stream(model)
    demo_thinking(reasoner)
    demo_tools(model)
    demo_meta(model)


if __name__ == "__main__":
    main()
