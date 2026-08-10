"""loop.py — 一個函式：讓 bot 一直跑到它不再叫工具（或預算用完）為止。

模型開口要工具 → 這裡真的去跑 → 結果餵回去 → 它再走一步，這樣一來一回。

**這一包不 import llms，也不 import tooljson。** `bot` 只要有 `ask()` 和
`pending_calls`，`dispatch` 只要是 `{名字: fn(**kwargs)}`。
"""

import asyncio

from .calling import perform
from .handle import Handle
from .limits import Limits

#: `limits.quiet > 1` 時，模型不叫工具就推它一句。只有調大過才會用到
NUDGE = "還沒做完的話就用工具動手，不要只描述你打算做什麼；做完了就直接講結論。"

#: 暫停中多久回頭看一次有沒有被放行
TICK = 0.1


async def run(bot, dispatch, prompt=None, handle=None, limits=None, images=None):
    """讓 bot 一直跑到它不再叫工具為止，回傳那個 `Handle`。

        h = agentloop.Handle()
        task = asyncio.create_task(agentloop.run(bot, dispatch, "把 a.txt 改一改", h))
        ...                                  # 另一條 routine 拿 h 問狀況、下指令
        await task

    同步要的話就 `asyncio.run(run(bot, dispatch, "..."))`。會擋住的兩件事
    （模型那次 HTTP、工具本體）都丟到 thread 去跑，所以 event loop 不會被卡住。

    `limits` 見 [limits.py](limits.py)，不給就是 `Limits()`（12 步，其餘不設限）。
    停的原因在 `handle.stop`：

        done      模型不叫工具了，講完了 —— 正常結束
        length    也不叫工具了，但它是被 max_tokens 切斷的，不是講完
        budget    步數用完了，它還在叫工具
        calls / time / tokens   那項預算用完了
        engine    bot 現在掛的思考引擎不在准用的清單裡（`handle.err`）
        error     llms 那邊回了錯（`handle.err`）
        stopped   外面叫它收手

    除了 done / length / error 以外，停下來時最後那批工具**還沒跑**，債留在
    `bot.history` 上。同一個 bot 再叫一次 `run()` 就從那裡接下去，工具不會跑兩遍。

    `prompt` 是第一句話，之後要插話用 `handle.say()`。`images` 只跟第一句一起送。

    **這裡不串流。** 逐字看是給人看的，這個迴圈是放著自己跑的，兩件事的用法對不上
    —— 要逐字看就自己 `bot.ask(stream=True)`（`../try.py` 就是那樣）。
    """
    h, lim = handle or Handle(), limits or Limits()
    h.begin(lim)

    # 上次跑到一半停掉的話，history 最後會是一則欠著工具結果的 assistant message，
    # 這時候直接再講一句會被 API 打回票。所以每一步都從「先還債」開始 —— 順便讓
    # 「再叫一次 run() 就接著跑」跟平常的迴圈走完全同一條路
    owed = bot.pending_calls
    h.say(prompt)  # 第一句話跟外面插的話走同一條路，所以不給 prompt 也行

    for _ in range(lim.steps):
        while h.paused and not h.stopping:
            h.phase = "paused"
            await asyncio.sleep(TICK)
        if h.stopping:
            return h.end("stopped")
        spent = lim.exhausted(h)
        if spent:
            return h.end(spent)
        wrong_engine = lim.engine_ok(bot)
        if wrong_engine:
            return h.end("engine", err=ValueError(wrong_engine))

        results = await _settle(owed, dispatch, h, lim) if owed else None
        h.next_step()
        reply = await asyncio.to_thread(
            bot.ask, prompt=h.take_say(), images=images, tool_results=results)
        images = None  # 圖只跟第一句一起送
        if not reply:
            return h.end("error", err=reply.err)
        h.spoke(reply.text, reply.usage)  # 沒串流，text 建好就在裡面了

        owed = reply.calls
        if owed:
            h.quiet = 0
            continue
        # 不叫工具了就是講完了 —— 除非它根本沒講完，是被 max_tokens 切斷的
        if reply.finish_reason == "length":
            return h.end("length")
        h.quiet += 1
        if h.quiet >= lim.quiet:
            return h.end("done")
        h.say(NUDGE)  # 只有 quiet 調大過才走到這：推它一把，再給一步機會

    return h.end("budget")


async def _settle(calls, dispatch, h, lim):
    """把上一步要求的工具全跑完，回 `{call_id: 結果字串}`。

    一次一個，照模型要求的順序 —— 併發跑省不了多少，卻會讓兩個 `run_shell`
    在同一個資料夾裡互相踩。**一個都不能漏**：少一個 key 下一步就對不起來，
    所以被預算擋下來的那些也要回一句話，不能不回。
    """
    results = {}
    for call in calls:
        h.doing(call)
        blocked = lim.allow(call.get("name"), h)
        if blocked:
            h.did(call, blocked, ran=False)
            results[call["id"]] = blocked
            continue
        out = await asyncio.to_thread(perform, dispatch, call)
        h.did(call, out)
        results[call["id"]] = out
    return results
