"""__main__.py — 拿真的 proxy 跑一遍，確認記憶、串流、思考、工具都還活著。

    python -m llms                  # 預設 deepseek-chat
    python -m llms lm-gemma-4-e4b   # 換模型
"""

import sys
import typing

from .client import LLM
from .func_schema import to_tools

URL = "http://localhost:4000"


def get_weather(city: str, unit: typing.Literal["celsius", "fahrenheit"] = "celsius"):
    """查詢指定城市的天氣。

    Args:
        city: 城市名稱
        unit: 溫度單位
    """
    return f"{city} 晴，26 度（{unit}）"


def demo_memory(model):
    print("== 對話記憶 ==")
    bot = LLM(url=URL, model=model)
    first, err = bot.ask("我的名字是小明，請記住。")
    print("第一句:", first, "err:", err)
    reply, err = bot.ask("我的名字是什麼？")
    print("回答:", reply, "err:", err)


def demo_stream(model):
    print("\n== 串流 ==")
    bot = LLM(url=URL, model=model)
    handler, err = bot.ask("用一句話介紹台北。", stream=True)
    if err:
        print("err:", err)
        return
    for ch in handler:
        print(ch, end="", flush=True)
    print("\nerr:", handler.err)


def demo_thinking(model):
    print("\n== 思考（非串流）==")
    bot = LLM(url=URL, model=model)
    reply, err = bot.ask("9.11 和 9.9 哪個大？只回答數字。")
    print("答案:", reply, "err:", err)
    thought = bot.last_reasoning
    print("思考:", (thought[:120] + "…") if thought else None)


def demo_tools(model):
    print("\n== 工具 ==")
    schemas, dispatch = to_tools(get_weather)
    bot = LLM(url=URL, model=model)
    if bot.supports_tools is False:
        print("這個模型宣告不支援 tool calling，跳過")
        return
    calls, err = bot.ask("台北天氣如何？", tools=schemas)
    print("calls:", calls, "err:", err)
    if not isinstance(calls, list):
        return
    results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in calls}
    reply, err = bot.ask(tool_results=results, tools=schemas)
    print("回答:", reply, "err:", err)


def main():
    model = sys.argv[1] if len(sys.argv) > 1 else "deepseek-chat"
    reasoner = sys.argv[2] if len(sys.argv) > 2 else "deepseek-reasoner"
    print(f"proxy: {URL}  model: {model}")
    demo_memory(model)
    demo_stream(model)
    demo_thinking(reasoner)
    demo_tools(model)


if __name__ == "__main__":
    main()
