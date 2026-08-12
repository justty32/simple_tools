"""Short live checks for the home models configured in proxy/litellm.yaml."""

import typing

from llms import Engine, LLM, Params, to_tools


URL = "http://127.0.0.1:4000"
MODELS = (
    "deepseek-chat",
    "deepseek-reasoner",
    "deepseek-v4-pro",
    "lm-gemma-4-12b",
    "lm-gemma-4-12b-nothink",
    "lm-gemma-4-e4b",
    "lm-gemma-4-e4b-nothink",
    "lm-qwen3.5-9b",
    "lm-qwen3.5-9b-nothink",
)


def get_weather(
    city: str,
    unit: typing.Literal["celsius", "fahrenheit"] = "celsius",
):
    """查詢指定城市的天氣。

    Args:
        city: 城市名稱
        unit: 溫度單位
    """
    return f"{city} 晴，26 度（{unit}）"


def bot(model, *, tools=None):
    # Thinking models can consume part of this allowance before emitting text.
    params = Params(temperature=0, max_tokens=256)
    return LLM(Engine(url=URL, model=model, params=params, timeout=180), tools=tools)


def require(reply, label):
    if reply.err:
        raise RuntimeError(f"{label}: {reply.err}")
    return reply


def basic(model):
    reply = require(bot(model).ask("Reply with exactly OK."), model)
    if not reply.text.strip():
        raise RuntimeError(f"{model}: empty response")
    print(f"PASS basic  {model}")


def streaming(model):
    reply = require(bot(model).ask("Reply with exactly STREAM.", stream=True), model)
    if not reply.text.strip() or reply.err:
        raise RuntimeError(f"{model} stream: {reply.err or 'empty response'}")
    print(f"PASS stream {model}")


def tool_roundtrip(model):
    schemas, dispatch = to_tools(get_weather)
    agent = bot(model, tools=schemas)
    first = require(
        agent.ask("Use the weather tool for Taipei, then report the result to me.",
                  tool_choice="required"),
        f"{model} tool call",
    )
    if not first.calls:
        raise RuntimeError(f"{model}: no required tool call returned")
    results = {call["id"]: dispatch[call["name"]](**call["args"]) for call in first.calls}
    final = require(agent.ask(tool_results=results), f"{model} tool result")
    if "26" not in final.text:
        raise RuntimeError(
            f"{model}: tool result was not reflected in response {final.text!r}"
        )
    print(f"PASS tools  {model}")


def main():
    Engine.clear_caps_cache()
    for model in MODELS:
        basic(model)
    for model in ("deepseek-chat", "lm-gemma-4-e4b"):
        streaming(model)
        tool_roundtrip(model)


if __name__ == "__main__":
    main()
