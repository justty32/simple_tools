"""Round 中追加圖片、工具、ask options 與 Step commit 語意。"""

import threading
from types import SimpleNamespace

import agentloop
from llms import Reply

from ._events import background, event, finish
from ._testing import FakeBot, TOOLS, check, response, wants


def dynamic_inputs():
    print("\n== Round 中重新配置下一個 Step ==")
    entered, release = threading.Event(), threading.Event()

    class BlockingBot(FakeBot):
        def ask(self, **kwargs):
            if not self.asked:
                entered.set()
                if not release.wait(1):
                    raise TimeoutError("測試沒有放行動態輸入 Step")
            return super().ask(**kwargs)

    schema = {
        "type": "function",
        "function": {"name": "new_tool", "description": "new", "parameters": {
            "type": "object", "properties": {}}},
    }
    ran = []
    handle = agentloop.Handle()
    bot = BlockingBot(response("先等等"), wants(("new", "new_tool", "{}")),
                      response("新工具跑完了"))
    dispatch = dict(TOOLS)
    runner, box = background(agentloop.run, bot, dispatch, "開始", handle)
    event(entered, "dynamic input model")
    handle.add_images("later.png")
    def new_tool():
        handle.set_ask_options(stream=None)
        ran.append("new")
        return "ok"

    handle.add_tools([schema], {"new_tool": new_tool})
    handle.set_ask_options(tool_choice="required", stream=True)
    release.set()
    result = finish(runner, box)

    check("圖片只送進下一個 Step", repr(bot.asked_images),
          "[None, ['later.png'], None]")
    check("新 schema 在下一個 Step 前生效",
          str([x["function"]["name"] for x in bot.tools]), "new_tool")
    check("新 dispatch 真能執行", f"{ran} {result.stop}", "['new'] done")
    check("ask options 從下一步起持續生效", repr(bot.asked_options),
          "[{}, {'tool_choice': 'required', 'stream': True}")
    check("ask option 傳 None 會從下一步清除",
          repr(bot.asked_options[2]), "{'tool_choice': 'required'}")

    for label, call, want in [
        ("schema/dispatch 不成對會拒絕",
         lambda: agentloop.Handle().add_tools([schema], {}), "same names"),
        ("loop 持有的 ask 欄位不能覆蓋",
         lambda: agentloop.Handle().set_ask_options(remember=False), "owns ask options"),
    ]:
        try:
            call()
        except ValueError as exc:
            got = str(exc)
        else:
            got = "not rejected"
        check(label, got, want)


def step_commit():
    chunk = SimpleNamespace(usage=None, choices=[SimpleNamespace(
        finish_reason=None,
        delta=SimpleNamespace(content="收到一半", reasoning_content=None,
                              tool_calls=None))])

    class BrokenStream:
        def __init__(self):
            self.first = True

        def __next__(self):
            if self.first:
                self.first = False
                return chunk
            raise RuntimeError("stream interrupted")

        def close(self):
            pass

    class PartialBot:
        pending_calls = []

        def ask(self, **kwargs):
            return Reply(BrokenStream(), stream=True)

    h = agentloop.run(PartialBot(), TOOLS, "開始")
    check("部分 message 仍完成一個 Step",
          f"{h.step} {h.text} {h.stop}", "1 收到一半 error")
    h = agentloop.run(FakeBot(RuntimeError("nothing received")), TOOLS, "開始")
    check("完全落空不算 Step", f"{h.step} {h.stop}", "0 error")
