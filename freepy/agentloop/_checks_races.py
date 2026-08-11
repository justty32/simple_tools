"""Cross-thread pause boundaries and callback snapshots."""

import threading

import agentloop
from agentloop.threading import start

from ._events import event
from ._testing import FakeBot, TOOLS, check, response, wants


def races():
    print("\n== 關鍵跨 thread 邊界 ==")
    entered, release = threading.Event(), threading.Event()

    class BlockingBot(FakeBot):
        def ask(self, **kwargs):
            entered.set()
            if not release.wait(1):
                raise TimeoutError("測試沒有放行 Step")
            return super().ask(**kwargs)

    ran = []
    h = agentloop.Handle()
    bot = BlockingBot(wants(("a", "work", "{}")), response("完成"))
    runner = start(bot, {"work": lambda: ran.append(1) or "ok"}, "開始", h)
    event(entered, "model")
    h.pause()
    release.set()
    check("Step 跑完才真正 paused", str(h.wait_for_state("paused", timeout=1)), "True")
    check("paused 前已提交 message，但尚未跑工具",
          f"step={h.step} calls={len(h.tool_calls)} ran={ran}", "step=1 calls=1 ran=[]")
    h.resume()
    result = runner.join()
    check("resume 後工具與下一 Step 完成", f"{result.stop} {ran}", "done [1]")

    seen = []
    h = agentloop.Handle()

    def first(state):
        seen.append("first")
        state.after_step.append(lambda _: seen.append("late"))

    h.after_step.extend([first, lambda _: seen.append("second")])
    agentloop.run(FakeBot(response("完成")), TOOLS, handle=h)
    check("callback 清單在邊界先取快照", repr(seen), "['first', 'second']")

    h = agentloop.Handle()
    with h.edit():
        h.prompt = "一起提交"
        h.ask_options["stream"] = True
    check("edit 提供 unrestricted RLock transaction", h.prompt, "一起提交")
