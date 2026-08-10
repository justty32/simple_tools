"""Handle one-shot、generic bot failure 與 Limits 輸入契約。"""

import threading

import agentloop
from agentloop import Limits

from ._events import background, event, finish
from ._testing import FakeBot, TOOLS, check, go, response, wants


def _concurrent_reuse():
    entered, release = threading.Event(), threading.Event()

    class BlockingBot(FakeBot):
        def ask(self, **kw):
            entered.set()
            if not release.wait(1):
                raise TimeoutError("測試沒有放行 one-shot model")
            return super().ask(**kw)

    handle = agentloop.Handle()
    first, box = background(
        agentloop.run, BlockingBot(response("第一個收工")), TOOLS, "第一個", handle)
    event(entered, "one-shot model")
    try:
        agentloop.run(FakeBot(response("不應啟動")), TOOLS, "第二個", handle)
    except RuntimeError as exc:
        rejected = str(exc)
    else:
        rejected = "not rejected"
    release.set()
    finish(first, box)
    return rejected


def contracts():
    print("\n== Handle 與 Limits 契約 ==")

    class ExplodingBot:
        pending_calls = []

        def ask(self, **kw):
            raise RuntimeError("裸 bot 爆掉")

    h = go(ExplodingBot())
    check("裸 bot 例外會收進 Handle",
          f"{h.stop} phase={h.phase} {h.err}", "error phase=error 裸 bot 爆掉")
    used = go(FakeBot(response("做完了")))
    try:
        go(FakeBot(response("不該啟動")), handle=used)
    except RuntimeError as exc:
        reuse = str(exc)
    else:
        reuse = "not rejected"
    check("Handle 是 one-shot，重用立即失敗", reuse, "handle already used")
    check("同時使用同一 Handle 也立即失敗",
          _concurrent_reuse(), "handle already used")

    attempts = go(FakeBot(
        wants(("missing", "no_such_tool", "{}"), ("bad", "one_arg", '{"y": 1}')),
        response("收工")))
    check("已交給 perform 的壞呼叫也消耗 calls/used",
          f"calls={attempts.calls} used={attempts.used}",
          "calls=2 used={'no_such_tool': 1, 'one_arg': 1}")

    invalid = [
        {"steps": None}, {"steps": 0}, {"steps": 1.5}, {"steps": True},
        {"calls": -1}, {"calls": 1.5}, {"seconds": -1}, {"seconds": float("nan")},
        {"tokens": -1}, {"tokens": 1.5}, {"per_tool": {"ok": -1}},
        {"per_tool": {"ok": 1.5}}, {"per_tool": []}, {"per_tool": {1: 2}},
        {"tools": "ok"}, {"tools": [1]}, {"engines": "deepseek-chat"},
        {"quiet": 0}, {"quiet": True},
    ]
    failures = []
    for kwargs in invalid:
        try:
            Limits(**kwargs)
        except ValueError:
            failures.append(kwargs)
    check("Limits 非法值在建構時 fail fast",
          f"{len(failures)}/{len(invalid)}", "19/19")
