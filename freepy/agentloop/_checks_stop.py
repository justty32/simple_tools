"""Callback PAUSE/END and tool-batch safe boundaries."""

import threading

import agentloop
from agentloop.threading import start

from ._events import event
from ._testing import FakeBot, check, response, wants


def stop_boundaries():
    print("\n== callback 控制與 tool batch 安全邊界 ==")
    h = agentloop.Handle()
    h.after_step.append(lambda _: agentloop.PAUSE)
    bot = FakeBot(wants(("a", "work", "{}")), response("完成"))
    ran = []
    runner = start(bot, {"work": lambda: ran.append(1) or "ok"}, "開始", h)
    check("after_step PAUSE 發生在 tools 前", str(h.wait_for_state("paused", timeout=1)),
          "True")
    check("tool 尚未執行", repr(ran), "[]")
    h.after_step.clear()
    h.resume()
    result = runner.join()
    check("恢復後照常完成", f"{result.stop} {ran}", "done [1]")

    entered, release = threading.Event(), threading.Event()
    batch = []

    def first():
        batch.append("first")
        entered.set()
        if not release.wait(1):
            raise TimeoutError("測試沒有放行工具")
        return "a"

    h = agentloop.Handle()
    bot = FakeBot(wants(("a", "first", "{}"), ("b", "second", "{}")),
                  response("完成"))
    runner = start(bot, {
        "first": first, "second": lambda: batch.append("second") or "b",
    }, "開始", h)
    event(entered, "tool batch")
    h.pause()
    release.set()
    check("tool batch 全部完成後才 paused",
          str(h.wait_for_state("paused", timeout=1)), "True")
    check("after_tools 前整批工具一個不漏", repr(batch), "['first', 'second']")
    check("結果尚未送進下一 Step", str(len(bot.asked)), "1")
    h.resume()
    runner.join()

    h = agentloop.Handle()
    h.after_step.append(lambda _: (_ for _ in ()).throw(RuntimeError("callback 爆掉")))
    result = agentloop.run(FakeBot(response("有提交")), {}, handle=h)
    check("callback exception 結束 Round", f"{result.stop} {result.err}",
          "error callback 爆掉")
