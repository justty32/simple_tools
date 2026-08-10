"""instruction/completion、FIFO 與 pause 競態關卡。"""

import threading

import agentloop

from ._events import background, event, finish, until
from ._testing import FakeBot, TOOLS, check, response, wants


def races():
    late = agentloop.Handle()
    late_bot = FakeBot(
        lambda: late.say("late instruction") or response("這步原本要收工"),
        response("收到追加指令才收工"))
    h = agentloop.run(late_bot, TOOLS, "做事", late)
    check("最後邊界的追加指令不會被吃掉",
          f"{h.stop} {h.step} {len(late_bot.asked)} {late_bot.asked[-1][0]}",
          "done 2 2 late instruction")
    try:
        h.say("太晚了")
    except RuntimeError as exc:
        rejected = str(exc)
    else:
        rejected = "not rejected"
    check("完成提交後的指令明確拒絕", rejected, "round already completed")

    entered, release = threading.Event(), threading.Event()

    class BlockingBot(FakeBot):
        def ask(self, **kw):
            if not self.asked:
                entered.set()
                if not release.wait(1):
                    raise TimeoutError("測試沒有放行第一個 Step")
            return super().ask(**kw)

    fifo, fifo_bot = agentloop.Handle(), BlockingBot(
        response("先不做事"), response("指令都收到了"))
    runner, box = background(agentloop.run, fifo_bot, TOOLS, "做事", fifo)
    event(entered, "FIFO model")
    gates = [threading.Event() for _ in range(5)]
    threads = []
    for i in range(4):
        def enqueue(n=i):
            gates[n].wait()
            fifo.say(f"instruction {n}")
            gates[n + 1].set()
        threads.append(threading.Thread(target=enqueue))
    for thread in threads:
        thread.start()
    gates[0].set()
    event(gates[-1], "FIFO enqueue")
    for thread in threads:
        thread.join()
    release.set()
    finish(runner, box)
    prompts = [prompt for prompt, _ in fifo_bot.asked]
    check("多 thread 追加指令保持 FIFO", repr(prompts),
          "instruction 0\\ninstruction 1\\ninstruction 2\\ninstruction 3")

    tool_entered, tool_release = threading.Event(), threading.Event()
    paused = agentloop.Handle()

    def pausing_tool():
        tool_entered.set()
        if not tool_release.wait(1):
            raise TimeoutError("測試沒有放行 tool")
        return "tool result"

    bot = FakeBot(wants(("p", "wait_control", "{}")), response("收工"))
    runner, box = background(
        agentloop.run, bot, {**TOOLS, "wait_control": pausing_tool}, "做事", paused)
    event(tool_entered, "pause tool")
    paused.pause()
    tool_release.set()
    until(lambda: "暫停中" in paused.now())
    before = len(bot.asked)
    paused.resume()
    result = finish(runner, box)
    check("tool 中 pause 真的擋住下一個 Step",
          f"before={before} after={len(bot.asked)} {result.stop}", "before=1 after=2 done")
