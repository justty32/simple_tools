"""loop.py — 一個阻塞函式：讓 bot 跑到它不再叫工具為止。

模型開口要工具 → 這裡真的去跑 → 結果餵回去 → 它再走一步，這樣一來一回。

**這一包不 import llms，也不 import tooljson。** `bot` 只要有 `ask()` 和
`pending_calls`，`dispatch` 只要是 `{名字: fn(**kwargs)}`。
"""

import time

from .calling import perform
from ._control import TICK, start_step, start_tools
from .handle import Handle
from ._inputs import apply_tool_updates
from .limits import Limits

#: `limits.quiet > 1` 時，模型不叫工具就推它一句。只有調大過才會用到
NUDGE = "還沒做完的話就用工具動手，不要只描述你打算做什麼；做完了就直接講結論。"


def run(bot, dispatch, prompt=None, handle=None, limits=None, images=None):
    """讓 bot 一直跑到它不再叫工具為止，回傳那個 `Handle`。

        h = agentloop.run(bot, dispatch, "把 a.txt 改一改")

    `run()` 是同步阻塞 API：直接等模型 HTTP 和工具完成。需要非同步的
    上層自己放進 thread/task，這一包不另外提供 async API。

    `limits` 見 [limits.py](limits.py)，不給就是 `Limits()`（12 步，其餘不設限）。
    停的原因在 `handle.stop`：

        done      模型不叫工具了，講完了 —— 正常結束
        length    也不叫工具了，但它是被 max_tokens 切斷的，不是講完
        budget    步數用完了，它還在叫工具
        calls / time / tokens   那項預算用完了
        engine    bot 現在掛的思考引擎不在准用的清單裡（`handle.err`）
        error     llms 那邊回了錯（`handle.err`）
        stopped   外面叫它收手

    預算在 tool batch 開始前用完，或 stop 在 model 中到達時，新 calls 留在
    `bot.history` 等下個 Round。stop 在 tools 中到達時則跑完整批，
    多用一個 settlement Step 送回 results，才停下且不執行新 calls。

    `prompt` 是第一句話，之後用 `handle.add_instruction()`（或短名
    `handle.say()`）追加。`images` 只跟第一個 Step 一起送。

    預設不串流；`handle.set_ask_options(stream=True)` 可以改 endpoint transport，
    但 `run()` 仍會阻塞收完，不負責把 chunks 廣播給外層。
    """
    h, lim = handle or Handle(), limits or Limits()
    # begin 和初始指令一起提交，不讓 stop/say 插在半啟動的縫裡。
    # 這一行刻意在 try 外：Handle 重用是 caller contract error，要 fail fast。
    h.begin(lim, prompt, images)

    try:
        # 上次跑到一半停掉的話，history 最後會是一則欠著工具結果的
        # assistant message。每一輪都先還這批債，續跑和平常迴圈才會走同一條路。
        owed = bot.pending_calls

        while True:
            results = None
            settlement = bool(owed)
            if settlement:
                if not start_tools(h, lim, bot):
                    return h
                # 一旦原子提交了 batch，中途的 pause/stop 都不切斷它。
                # 全部 results 必須成為下一個 Step，才不會在續跑時重做副作用。
                results = _settle(owed, dispatch, h, lim)

            started, step_prompt, step_images, updates, ask_options = start_step(
                h, lim, bot, settlement)
            if not started:
                return h
            apply_tool_updates(bot, dispatch, updates)

            reply = bot.ask(prompt=step_prompt, images=step_images,
                            tool_results=results, **ask_options)
            text = getattr(reply, "text", "")
            calls = getattr(reply, "calls", [])
            finish_reason = getattr(reply, "finish_reason", None)
            committed = bool(text or calls or finish_reason is not None)
            if not committed:
                err = getattr(reply, "err", None) or RuntimeError("bot.ask() returned no message")
                return h.end("error", err=err)

            h.spoke(text, getattr(reply, "usage", None))
            if getattr(reply, "err", None) is not None:
                return h.end("error", err=reply.err)
            owed = calls

            # stop during model：message 已收進 history，但不執行它新要的 tools。
            # stop during tools：上面已送完 settlement Step，也在這裡停，不跑新 calls。
            if h.checkpoint() == "stopped":
                return h.end("stopped")

            if owed:
                h.reset_quiet()
                continue

            # completion 與 say() 在 Handle 裡共用同一把 lock。pause 時只重問
            # 這個完成邊界，不會多送一個 Step，quiet 也不會重複計數。
            while True:
                action = h.finish_or_continue(finish_reason, lim.quiet, NUDGE)
                if action == "ended":
                    return h
                if action == "continue":
                    break
                time.sleep(TICK)

    except Exception as err:
        # LLM.ask() 一般會自己回錯誤 Reply，但 duck-typed bot 可能直接拋例外。
        return h.end("error", err=err)


def _settle(calls, dispatch, h, lim):
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
        out = perform(dispatch, call)
        h.did(call, out)
        results[call["id"]] = out
    return results
