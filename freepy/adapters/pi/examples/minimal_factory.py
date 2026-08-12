"""Deterministic offline factory for exercising the Pi bridge."""


class Reply:
    def __init__(self, text, calls=(), finish_reason="stop"):
        self.text = text
        self.calls = list(calls)
        self.finish_reason = finish_reason
        self.usage = {
            "prompt": 4,
            "completion": 2,
            "total": 6,
            "cached": 0,
        }
        self.err = None


class DemoBot:
    pending_calls = []
    tools = None

    def __init__(self, tool_text):
        self.tool_text = tool_text
        self.history = []
        self.turn = 0

    def ask(self, prompt=None, images=None, tool_results=None, **options):
        self.turn += 1
        self.history.append({
            "prompt": prompt,
            "images": images,
            "tool_results": tool_results,
            "options": options,
        })
        if self.turn == 1:
            return Reply(
                "I will call echo.",
                [{
                    "id": "demo_call",
                    "name": "echo",
                    "args": {"text": self.tool_text},
                }],
                "tool_calls",
            )
        return Reply(f"Tool result: {(tool_results or {}).get('demo_call')}")


def create(config=None):
    """Return the bridge factory contract: ``(bot, dispatch)``."""
    config = config or {}
    bot = DemoBot(config.get("tool_text", "original text"))
    dispatch = {"echo": lambda text: f"echo: {text}"}
    return bot, dispatch
