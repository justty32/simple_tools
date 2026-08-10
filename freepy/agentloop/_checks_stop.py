"""model/tool 中 stop、settlement 與 error priority 關卡。"""

import threading

import agentloop

from ._events import background, event, finish
from ._testing import FakeBot, TOOLS, check, wants


def stop_boundaries():
    model_entered, model_release = threading.Event(), threading.Event()

    class StoppableBot(FakeBot):
        def ask(self, **kw):
            model_entered.set()
            if not model_release.wait(1):
                raise TimeoutError("測試沒有放行 model")
            return super().ask(**kw)

    handle, model_ran = agentloop.Handle(), []
    bot = StoppableBot(wants(("never", "must_not_run", "{}")))
    runner, box = background(
        agentloop.run, bot, {"must_not_run": lambda: model_ran.append("ran")}, "做事", handle)
    event(model_entered, "stoppable model")
    handle.ask_stop()
    try:
        handle.say("停止後不應接受")
    except RuntimeError as exc:
        rejected = str(exc)
    else:
        rejected = "not rejected"
    model_release.set()
    model = finish(runner, box)
    check("model 中 stop 不執行新 tool calls",
          f"{model.stop} step={model.step} ran={model_ran}", "stopped step=1 ran=[]")
    check("stop 接受後的新指令明確拒絕", rejected, "round is stopping")

    error_entered, error_release = threading.Event(), threading.Event()

    class ErrorBot:
        pending_calls = []

        def ask(self, **kw):
            error_entered.set()
            if not error_release.wait(1):
                raise TimeoutError("測試沒有放行 error model")
            raise RuntimeError("端點同時壞掉")

    handle = agentloop.Handle()
    runner, box = background(agentloop.run, ErrorBot(), TOOLS, "做事", handle)
    event(error_entered, "error model")
    handle.ask_stop()
    error_release.set()
    error = finish(runner, box)
    check("端點 error 優先於同時的 stop", f"{error.stop} {error.err}", "error 端點同時壞掉")

    batch_entered, batch_release = threading.Event(), threading.Event()
    batch_ran, handle = [], agentloop.Handle()

    def first():
        batch_ran.append("first")
        batch_entered.set()
        if not batch_release.wait(1):
            raise TimeoutError("測試沒有放行 tool batch")
        return "first result"

    bot = FakeBot(
        wants(("a", "first", "{}"), ("b", "second", "{}")),
        wants(("c", "must_not_run", "{}")))
    runner, box = background(agentloop.run, bot, {
        "first": first,
        "second": lambda: batch_ran.append("second") or "second result",
        "must_not_run": lambda: batch_ran.append("forbidden"),
    }, "做事", handle)
    event(batch_entered, "stoppable tool batch")
    handle.say("排過但 stop 優先")
    handle.ask_stop()
    batch_release.set()
    batch = finish(runner, box)
    settled = sorted((bot.asked[1][1] or {}).keys()) if len(bot.asked) > 1 else []
    check("tool batch 中 stop 會完整 settlement 且不跑新 calls",
          f"{batch.stop} step={batch.step} ran={batch_ran} results={settled} prompt={bot.asked[1][0]}",
          "stopped step=2 ran=['first', 'second'] results=['a', 'b'] prompt=None")
