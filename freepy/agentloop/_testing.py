"""離線驗證共用的假 bot、Reply fixture 與 tools。"""

import time
from types import SimpleNamespace

import agentloop
from llms import Bot, Reply

FAILED = []


def check(label, got, want):
    ok = want in got if isinstance(want, str) else want(got)
    if not ok:
        FAILED.append(label)
    print(f"  {'ok  ' if ok else 'FAIL'} {label}: {got[:76]!r}")


def response(text=None, calls=(), finish="stop", total=10, *, prompt=None,
             completion=None, cached=None):
    prompt = total - 1 if prompt is None else prompt
    completion = 1 if completion is None else completion
    message = SimpleNamespace(content=text, reasoning_content=None, tool_calls=[
        SimpleNamespace(id=i, function=SimpleNamespace(name=n, arguments=a))
        for i, n, a in calls] or None)
    return SimpleNamespace(
        choices=[SimpleNamespace(message=message, finish_reason=finish)],
        usage=SimpleNamespace(
            prompt_tokens=prompt,
            completion_tokens=completion,
            total_tokens=total,
            prompt_tokens_details=SimpleNamespace(cached_tokens=cached),
        ))


def wants(*calls):
    return response("好，我做", calls, finish="tool_calls")


def loop(name="ok", n=20):
    return [wants((f"c{i}", name, "{}")) for i in range(n)]


class FakeBot(Bot):
    """真的 Bot，只是 ask() 不出門，照劇本吐真的 Reply。"""

    def __init__(self, *script):
        super().__init__()
        self.script = list(script)
        self.asked = []
        self.asked_images = []
        self.asked_options = []

    def ask(self, prompt=None, images=None, tool_results=None, **kw):
        self.asked.append((prompt, tool_results))
        self.asked_images.append(images)
        self.asked_options.append(kw)
        for call_id, out in (tool_results or {}).items():
            self.history.append({"role": "tool", "tool_call_id": call_id, "content": out})
        if prompt:
            self.history.append({"role": "user", "content": prompt})
        if not self.script:
            return Reply(None, err=RuntimeError("劇本用完了"))
        item = self.script.pop(0)
        if callable(item):
            item = item()
        if isinstance(item, Exception):
            return Reply(None, err=item)
        return Reply(item, self, remember=True)


def go(bot, dispatch=None, prompt="做事", **kw):
    return agentloop.run(bot, dispatch if dispatch is not None else TOOLS, prompt, **kw)


ran = []
TOOLS = {
    "ok": lambda: ran.append("ok") or "做完了",
    "boom": lambda: 1 / 0,
    "one_arg": lambda x: f"拿到 {x}",
    "huge": lambda: "x" * (agentloop.MAX_OUTPUT + 500),
    "nothing": lambda: None,
    "slow": lambda: time.sleep(0.12) or "慢慢做完了",
}
