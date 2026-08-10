"""`loop.run()` 在 tool batch 與 Step 開始前的 cooperative checkpoint。"""

import time

TICK = 0.1


def preflight(h, lim, bot):
    if h.step >= lim.steps:
        return h.end("budget")
    spent = lim.exhausted(h)
    if spent:
        return h.end(spent)
    wrong_engine = lim.engine_ok(bot)
    if wrong_engine:
        return h.end("engine", err=ValueError(wrong_engine))
    return None


def start_tools(h, lim, bot):
    """等到能原子開始一批 pending tools；回傳是否真的開始。"""
    while True:
        control = h.checkpoint()
        if control == "stopped":
            h.end("stopped")
            return False
        if control == "paused":
            time.sleep(TICK)
            continue
        if preflight(h, lim, bot):
            return False
        state = h.start_tools()
        if state == "started":
            return True
        if state == "stopped":
            h.end("stopped")
            return False
        time.sleep(TICK)


def start_step(h, lim, bot, settlement):
    """等到能原子送出一個 Step，並一起取走追加輸入 FIFO。"""
    while True:
        if not settlement:
            control = h.checkpoint()
            if control == "stopped":
                h.end("stopped")
                return False, None, None, None, None
            if control == "paused":
                time.sleep(TICK)
                continue
            if preflight(h, lim, bot):
                return False, None, None, None, None

        state, prompt, images, tools, options = h.start_step(settlement=settlement)
        if state == "started":
            return True, prompt, images, tools, options
        if state == "stopped":
            h.end("stopped")
            return False, None, None, None, None
        time.sleep(TICK)
