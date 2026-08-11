"""Parked runner, waiting and live controls."""

import agentloop
from agentloop.threading import start

from ._testing import FakeBot, TOOLS, check, response


def steering():
    print("\n== auto_finish=False 的 parked runner ==")
    h = agentloop.Handle(auto_finish=False)
    bot = FakeBot(response("先停在這裡"), response("真的完成"))
    runner = start(bot, TOOLS, "開始", h)
    check("模型自然靜止後進 waiting", str(h.wait_for_state("waiting", timeout=1)), "True")
    check("waiting 時 run 還沒返回", str(runner.is_alive()), "True")
    with h.edit():
        h.prompt = "再跑一步"
        h.auto_finish = True
    check("waiting 接受 resume", str(h.resume()), "True")
    result = runner.join()
    check("同一 runner 被喚醒並完成", f"{result.stop} {result.step}", "done 2")
    check("公開修改成為下一 Step 輸入", str(bot.asked[1][0]), "再跑一步")


def resuming():
    print("\n== pause 不切斷 operation ==")
    h = agentloop.Handle(auto_finish=False)
    h.pause()
    bot = FakeBot(response("恢復後才問到"))
    runner = start(bot, TOOLS, "開始", h)
    check("開始前 pause 會 park", str(h.wait_for_state("paused", timeout=1)), "True")
    check("paused 時還沒呼叫模型", str(len(bot.asked)), "0")
    h.auto_finish = True
    h.resume()
    result = runner.join()
    check("resume 後同一 run 完成", f"{result.stop} {len(bot.asked)}", "done 1")
