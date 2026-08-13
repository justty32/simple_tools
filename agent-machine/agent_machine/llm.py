"""LLM engines. The first offline engine only echoes the last user message."""


def ask(llm: dict, messages: list, tools: list) -> dict:
    engine = llm.get("engine")
    if engine != "echo":
        raise ValueError(f"不支援的 LLM engine: {engine!r}")
    for message in reversed(messages):
        if isinstance(message, dict) and message.get("role") == "user":
            content = message.get("content")
            if not isinstance(content, str):
                raise ValueError("echo engine 的 user message content 必須是字串")
            return {"role": "assistant", "content": content}
    raise ValueError("messages 中沒有 user message")
