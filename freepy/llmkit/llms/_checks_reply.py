"""Offline Reply shape checks; no endpoint or SDK objects required."""

from types import SimpleNamespace as NS

from .reply import Reply


class Response:
    def __init__(self, choices, usage=None):
        self.choices = choices
        self.usage = usage
        self.closed = False

    def close(self):
        self.closed = True


class Stream:
    def __init__(self, *chunks):
        self.chunks = iter(chunks)
        self.closed = False

    def __iter__(self):
        return self

    def __next__(self):
        return next(self.chunks)

    def close(self):
        self.closed = True


def choice(message=None, delta=None, finish="stop"):
    return NS(message=message, delta=delta, finish_reason=finish)


def check(condition, label):
    if not condition:
        raise AssertionError(label)
    print(f"ok  {label}")


def main():
    ollama = Response([choice(NS(content="", reasoning="先想", tool_calls=[]), finish="length")])
    reply = Reply(ollama)
    check(reply.reasoning == "先想" and reply.text == "", "Ollama reasoning alias")
    check(reply.finish_reason == "length" and ollama.closed, "non-stream metadata and close")

    standard = Response([choice(NS(
        content="答案", reasoning_content="標準", reasoning="別重複", tool_calls=[]
    ))])
    reply = Reply(standard)
    check(reply.reasoning == "標準" and reply.text == "答案", "reasoning_content precedence")

    stream = Stream(
        NS(usage=None, choices=[choice(delta=NS(
            reasoning="逐", reasoning_content=None, content=None, tool_calls=None
        ), finish=None)]),
        NS(usage=None, choices=[choice(delta=NS(
            reasoning=None, reasoning_content=None, content="答", tool_calls=None
        ))]),
    )
    reply = Reply(stream, stream=True)
    check(list(reply.parts()) == [("think", "逐"), ("answer", "答")],
          "stream alias and part ordering")
    check(reply.reasoning == "逐" and reply.text == "答" and stream.closed,
          "stream values and close")
    print("llms Reply: all checks passed")


if __name__ == "__main__":
    main()
