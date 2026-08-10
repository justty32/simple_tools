"""Handle live control 與 pending debt 關卡。"""

import time

import agentloop
from agentloop import Limits

from ._events import background, finish, until
from ._testing import FakeBot, TOOLS, check, go, loop, ran, response, wants


def steering():
    print("\n== 一邊跑一邊問、一邊控制 ==")
    h, seen = agentloop.Handle(), []

    def wait():
        time.sleep(0.05)
        h.say("順便看一下 b.txt")
        h.pause()
        return "看完了"

    bot = FakeBot(wants(("a", "wait", "{}")), wants(("b", "ok", "{}")),
                  response("好，收工"))
    thread, box = background(agentloop.run, bot, {**TOOLS, "wait": wait}, "做事", h)
    until(lambda: h.phase == "paused")
    while not h.done() and len(seen) < 3:
        seen.append(h.now())
        time.sleep(0.001)
    paused = h.now()
    h.resume()
    result = finish(thread, box)
    check("其他 thread 問得到目前工作", " ".join(seen + [paused]), "暫停中")
    check("暫停中看得出來", paused, "暫停中")
    check("插的話真的送到模型那邊", str(bot.asked[1][0]), "順便看一下 b.txt")
    check("放行之後跑完", f"{result.stop} {result.step}", "done 3")

    handle = agentloop.Handle()
    tools = {**TOOLS, "ok": lambda: handle.ask_stop() or "順便喊停"}
    h = go(FakeBot(*loop()), tools, handle=handle)
    check("外面喊停就收手", f"{h.stop} {h.step}", "stopped 2")


def resuming():
    print("\n== 停在一半，接著跑 ==")
    ran.clear()
    bot = FakeBot(wants(("a", "ok", "{}")))
    h = go(bot, limits=Limits(steps=1))
    check("預算用完時那批工具還沒跑", f"{h.stop} {len(ran)}", "budget 0")
    check("債留在 history 上", str([c["name"] for c in bot.pending_calls]), "['ok']")
    bot.script.append(response("這次做完了"))
    h = go(bot, prompt=None)
    check("同一個 bot 再叫一次就接著跑", f"{h.stop} {len(ran)} {h.text}", "done 1 這次做完了")
