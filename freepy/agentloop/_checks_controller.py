"""Offline checks for ``advance()`` and the local Controller convenience API."""

import threading

import agentloop

from . import threading as loop_threading
from ._testing import FakeBot, TOOLS, check, response, wants


def controller():
    print("\n== Controller：本地便利封裝 ==")
    bot = FakeBot(wants(("a", "ok", "{}")), response("完成"))
    c = agentloop.Controller(bot, TOOLS, "開始")
    check("Controller 保留直接 Handle", str(c.handle.state), "idle")
    c.advance()
    check("第一次 advance 只做 Step", f"{c.handle.step} {c.handle.calls}", "1 0")
    c.advance()
    check("第二次 advance 才跑 tools", f"{c.handle.step} {c.handle.calls}", "1 1")
    c.advance()
    check("逐步 Controller 可完成", f"{c.state} {c.handle.stop}", "completed done")

    c = agentloop.Controller(FakeBot(response("背景完成")), TOOLS)
    c.start(name="agentloop-check")
    result = c.join()
    check("背景 Controller 返回同一個 Handle", str(result is c.handle), "True")
    check("背景 Controller 已完成", result.stop, "done")

    c = agentloop.Controller(
        FakeBot(response("先等待"), response("互動完成")), TOOLS,
        handle=agentloop.Handle(auto_finish=False))
    c.start()
    c.handle.wait_for_state("waiting", timeout=1)
    sent = c.send("繼續", finish=True)
    result = c.join(1)
    check("send 在 parked boundary 設定下一個 prompt 並恢復",
          f"{sent is c} {c.bot.asked[1][0]} {result.text} {result.stop}",
          "True 繼續 互動完成 done")

    try:
        c.send("太晚了")
    except RuntimeError:
        rejected = True
    else:
        rejected = False
    check("send 不假裝是任意時刻可投遞的 queue", str(rejected), "True")

    c = agentloop.Controller(FakeBot(response("x")), TOOLS)
    c.advance()
    try:
        c.start()
    except RuntimeError:
        mixed = True
    else:
        mixed = False
    check("不混用兩個 runner 樣式", str(mixed), "True")


def advance_protocol():
    print("\n== advance：一次只跨一個 safe boundary ==")
    h = agentloop.Handle()
    bot = FakeBot(wants(("a", "ok", "{}")), response("完成"))
    agentloop.advance(bot, TOOLS, "開始", h)
    check("第一個 advance 只做 Step", f"{h.step} {h.calls} {h.state}", "1 0 ready")
    agentloop.advance(handle=h)
    check("第二個 advance 才做完整 tool batch", f"{h.step} {h.calls} {h.state}", "1 1 ready")
    agentloop.advance(handle=h)
    check("第三個 advance 才做下一 Step", f"{h.step} {h.stop}", "2 done")

    h = agentloop.Handle()
    try:
        agentloop.advance(handle=h)
    except ValueError:
        missing_bot = True
    else:
        missing_bot = False
    check("第一次漏傳 bot 會 fail fast", f"{missing_bot} {h.state}", "True idle")

    h = agentloop.advance(FakeBot(response("完成")), TOOLS)
    try:
        agentloop.advance(FakeBot(response("不應執行")), handle=h)
    except ValueError:
        late_inputs = True
    else:
        late_inputs = False
    check("後續參數誤用不會毀掉結果",
          f"{late_inputs} {h.state} {h.stop} {h.err}",
          "True completed done None")


def begin_is_atomic():
    print("\n== runner 初始化：先 snapshot，再占用 Handle ==")

    class ExplodingCalls:
        def __iter__(self):
            raise RuntimeError("pending calls iterator exploded")

    class BrokenBeginBot:
        def __init__(self, failure):
            self.failure = failure

        @property
        def history(self):
            if self.failure == "history":
                raise RuntimeError("history getter exploded")
            return []

        @property
        def tools(self):
            if self.failure == "tools":
                raise RuntimeError("tools getter exploded")
            return []

        @property
        def pending_calls(self):
            if self.failure == "pending":
                return ExplodingCalls()
            return []

    def failed_cleanly(invoke, handle):
        try:
            invoke()
        except RuntimeError as err:
            message = str(err)
        else:
            message = "did not raise"
        clean = (handle.state == "idle" and not handle.done()
                 and handle._runner is None and handle._started is None
                 and handle.elapsed() == 0.0)
        return message, clean

    h = agentloop.Handle()
    message, clean = failed_cleanly(
        lambda: agentloop.advance(BrokenBeginBot("history"), handle=h), h)
    agentloop.advance(FakeBot(response("重試完成")), TOOLS, handle=h)
    check("advance 的 history getter 失敗不會半占用 Handle",
          f"{message} {clean} {h.stop}",
          "history getter exploded True done")

    h = agentloop.Handle()
    message, clean = failed_cleanly(
        lambda: agentloop.run(BrokenBeginBot("tools"), handle=h), h)
    agentloop.run(FakeBot(response("重試完成")), TOOLS, handle=h)
    check("run 的 tools getter 失敗後同一 Handle 可重試",
          f"{message} {clean} {h.stop}",
          "tools getter exploded True done")

    h = agentloop.Handle()
    runner = loop_threading.start(BrokenBeginBot("pending"), handle=h)
    message, clean = failed_cleanly(runner.join, h)
    agentloop.run(FakeBot(response("重試完成")), TOOLS, handle=h)
    check("背景 pending_calls iterator 失敗會由 join 帶回",
          f"{message} {clean} {h.stop}",
          "pending calls iterator exploded True done")


def advance_boundaries():
    print("\n== advance：parked 修改與 runner ownership ==")

    after_tools = []
    h = agentloop.Handle()
    h.after_step.append(lambda _: agentloop.PAUSE)
    h.after_tools.append(lambda _: after_tools.append("tools"))
    bot = FakeBot(wants(("a", "work", "{}")), response("不應再問"))
    agentloop.advance(bot, {"work": lambda: "ran"}, handle=h)
    with h.edit():
        h.tool_calls.clear()
    h.resume()
    agentloop.advance(handle=h)
    check("Step 後暫停時清掉 calls 會直接完成",
          f"{h.stop} {len(bot.asked)} {after_tools}", "done 1 []")

    ran = []
    h = agentloop.Handle(auto_finish=False)
    bot = FakeBot(response("先等"), response("完成"))
    agentloop.advance(bot, {"work": lambda: ran.append(1) or "ok"}, handle=h)
    with h.edit():
        h.tool_calls.append({"id": "a", "name": "work", "args": {}})
        h.auto_finish = True
    h.resume()
    agentloop.advance(handle=h)
    agentloop.advance(handle=h)
    check("waiting 時新增 call 會先執行工具",
          f"{ran} {bot.asked[1][1]} {h.stop}", "[1] {'a': 'ok'} done")

    h = agentloop.Handle()
    h.after_tools.append(lambda _: agentloop.PAUSE)
    bot = FakeBot(wants(("a", "work", "{}")), response("完成"))
    agentloop.advance(bot, {"work": lambda: "original"}, handle=h)
    agentloop.advance(handle=h)
    with h.edit():
        h.tool_results["a"] = "rewritten"
    h.resume()
    agentloop.advance(handle=h)
    check("tools 後暫停可改寫送入下一 Step 的結果",
          f"{bot.asked[1][1]} {h.stop}", "{'a': 'rewritten'} done")

    entered = threading.Event()
    release = threading.Event()

    class BlockingBot(FakeBot):
        def ask(self, **kwargs):
            entered.set()
            if not release.wait(1):
                raise TimeoutError("測試沒有放行 Step")
            return super().ask(**kwargs)

    h = agentloop.Handle()
    bot = BlockingBot(response("完成"))
    first = threading.Thread(target=agentloop.advance,
                             args=(bot, TOOLS, None, h))
    first.start()
    entered.wait(1)
    try:
        agentloop.advance(handle=h)
    except RuntimeError:
        wrong_runner = True
    else:
        wrong_runner = False
    release.set()
    first.join(1)
    check("第二條 thread 不能重複執行同一 Handle",
          f"{wrong_runner} {len(bot.asked)} {h.stop}", "True 1 done")

    class BrokenToolsBot(FakeBot):
        def __init__(self, *script):
            self._reject_tools = False
            self._tools = None
            super().__init__(*script)
            self._reject_tools = True

        @property
        def tools(self):
            return self._tools

        @tools.setter
        def tools(self, value):
            if self._reject_tools:
                raise RuntimeError("tools setter exploded")
            self._tools = value

    h = agentloop.Handle()
    h.tools = []
    agentloop.advance(BrokenToolsBot(response("不應執行")), TOOLS, handle=h)
    check("準備 operation 失敗會結束而非卡在 running",
          f"{h.state} {h.stop} {h.err}",
          "error error tools setter exploded")

    c = agentloop.Controller(FakeBot(response("背景完成")), TOOLS)
    gate = threading.Barrier(3)
    entered = threading.Event()
    entered_twice = threading.Event()
    release_start = threading.Event()
    start_calls = 0
    start_calls_lock = threading.Lock()
    start_errors = []
    original_start = loop_threading.start

    def delayed_start(*args, **kwargs):
        nonlocal start_calls
        with start_calls_lock:
            start_calls += 1
            entered.set()
            if start_calls > 1:
                entered_twice.set()
        if not release_start.wait(1):
            raise TimeoutError("測試沒有放行背景 start")
        return original_start(*args, **kwargs)

    def start_together():
        gate.wait()
        try:
            c.start()
        except BaseException as err:
            start_errors.append(err)

    loop_threading.start = delayed_start
    try:
        starters = [threading.Thread(target=start_together) for _ in range(2)]
        for starter in starters:
            starter.start()
        gate.wait()
        entered.wait(1)
        entered_twice.wait(0.2)
        release_start.set()
        for starter in starters:
            starter.join(1)
    finally:
        release_start.set()
        loop_threading.start = original_start
    result = c.join()
    check("並行 start 仍只保留一個成功 runner",
          f"{result.stop} {len(c.bot.asked)} {start_calls} {len(start_errors)}",
          "done 1 1 0")

    h = agentloop.Handle()
    agentloop.advance(FakeBot(wants(("a", "ok", "{}"))), TOOLS, handle=h)
    paused = h.pause()
    check("ready 已是安全邊界，pause 立即生效",
          f"{paused} {h.state} {h.wait_until_paused(0)}",
          "True paused True")

    h = agentloop.Handle()
    agentloop.advance(FakeBot(wants(("a", "ok", "{}"))), TOOLS, handle=h)
    ended = h.end(reason="operator")
    check("ready 已是安全邊界，end 立即完成",
          f"{ended} {h.state} {h.stop} {h.done()}",
          "True completed operator True")
