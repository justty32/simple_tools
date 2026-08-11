"""Public mutable inputs and Step commit semantics."""

from types import SimpleNamespace

import agentloop
from llms import Reply

from ._testing import FakeBot, TOOLS, check, response


def dynamic_inputs():
    print("\n== 公開狀態直接增刪查改 ==")
    h = agentloop.Handle()
    h.prompt = "Handle 上的 prompt"
    h.images = ["screen.png"]
    h.ask_options["tool_choice"] = "required"
    bot = FakeBot(response("完成"))
    result = agentloop.run(bot, TOOLS, handle=h)
    check("公開 prompt 直接成為 Step 輸入", str(bot.asked[0][0]), "Handle 上的 prompt")
    check("公開 images 直接成為 Step 輸入", repr(bot.asked_images), "screen.png")
    check("公開 ask_options 直接成為 Step 輸入", repr(bot.asked_options),
          "tool_choice")
    check("正常完成", result.stop, "done")

    h = agentloop.Handle()
    h.after_step.append(lambda state: setattr(state, "prompt", "不應隱含再跑一步"))
    bot = FakeBot(response("本來就完成"), response("不應執行"))
    result = agentloop.run(bot, TOOLS, "開始", h)
    check("修改下一步資料不會迫使 Round 繼續", f"{result.step} {len(bot.asked)}",
          "1 1")


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
    check("部分 message 仍提交一個 Step", f"{h.step} {h.text} {h.stop}",
          "1 收到一半 error")
    h = agentloop.run(FakeBot(RuntimeError("nothing received")), TOOLS, "開始")
    check("完全落空不提交 Step", f"{h.step} {h.stop}", "0 error")
