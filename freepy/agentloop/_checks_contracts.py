"""Handle ownership and input validation contracts."""

import threading

import agentloop
from agentloop.limits import Limits
from agentloop.threading import start

from ._events import event
from ._testing import FakeBot, TOOLS, check, response


def _concurrent_reuse():
    entered, release = threading.Event(), threading.Event()

    class BlockingBot(FakeBot):
        def ask(self, **kw):
            entered.set()
            if not release.wait(1):
                raise TimeoutError("測試沒有放行 model")
            return super().ask(**kw)

    handle = agentloop.Handle()
    first = start(BlockingBot(response("第一個")), TOOLS, "第一個", handle)
    event(entered, "one runner")
    try:
        agentloop.run(FakeBot(response("不應啟動")), TOOLS, "第二個", handle)
    except RuntimeError as exc:
        rejected = str(exc)
    else:
        rejected = "not rejected"
    release.set()
    first.join()
    return rejected


def contracts():
    print("\n== Handle ownership 與契約 ==")
    check("同一 Handle 只准一個 runner", _concurrent_reuse(), "handle already used")
    h = agentloop.run(FakeBot(response("完成")), TOOLS)
    check("completed Handle 不可重跑",
          _reuse(h), "handle already used")
    failed = start(FakeBot(response("不應啟動")), TOOLS, handle=h)
    try:
        failed.join()
    except RuntimeError as exc:
        background_error = str(exc)
    else:
        background_error = "not raised"
    check("threading 工具會把 runner 啟動錯誤帶回來", background_error,
          "handle already used")
    check("running 狀態的 resume 是明確 no-op", str(agentloop.Handle().resume()), "False")
    try:
        agentloop.Handle().pause(safe=False)
    except ValueError as exc:
        unsafe = str(exc)
    else:
        unsafe = "not rejected"
    check("不假裝能 unsafe pause", unsafe, "only supports safe")

    invalid = [
        {"steps": None}, {"steps": 0}, {"steps": 1.5}, {"steps": True},
        {"calls": -1}, {"calls": 1.5}, {"seconds": -1},
        {"seconds": float("nan")},
        {"input_tokens": -1}, {"input_tokens": 1.5},
        {"output_tokens": -1}, {"output_tokens": 1.5},
        {"per_tool": {"ok": -1}}, {"per_tool": {"ok": 1.5}},
        {"per_tool": []}, {"per_tool": {1: 2}}, {"tools": "ok"},
        {"tools": [1]}, {"engines": "deepseek-chat"},
    ]
    failures = []
    for kwargs in invalid:
        try:
            Limits(**kwargs)
        except ValueError:
            failures.append(kwargs)
    check("Limits 非法值 fail fast", f"{len(failures)}/{len(invalid)}", "19/19")


def _reuse(handle):
    try:
        agentloop.run(FakeBot(response("不應啟動")), TOOLS, handle=handle)
    except RuntimeError as exc:
        return str(exc)
    return "not rejected"
