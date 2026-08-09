"""__main__.py — 離線煙霧測試：假的 bot，真的 Reply，真的迴圈。

    PYTHONPATH=llmkit uv run python -m agentloop
    PYTHONPATH=llmkit uv run python -m agentloop deepseek-chat   # 再多一關：真模型

沒給模型名字就不連 proxy、不碰網路。劇本是幾包假的「openai 回來的東西」，餵給**真的** `Reply`，
所以迴圈走的路跟接真模型時一模一樣 —— 差別只在 `ask()` 不出門。
"""

import asyncio
import sys
import time
from types import SimpleNamespace

import agentloop
from agentloop import Limits
from llms import LLM, Reply

FAILED = []


def _check(label, got, want):
    ok = want in got if isinstance(want, str) else want(got)
    if not ok:
        FAILED.append(label)
    print(f"  {'ok  ' if ok else 'FAIL'} {label}: {got[:76]!r}")


def _response(text=None, calls=(), finish="stop", total=10):
    """假一包 openai 回來的東西。calls 是 [(id, 名字, arguments 字串)]。"""
    message = SimpleNamespace(content=text, reasoning_content=None, tool_calls=[
        SimpleNamespace(id=i, function=SimpleNamespace(name=n, arguments=a))
        for i, n, a in calls] or None)
    return SimpleNamespace(
        choices=[SimpleNamespace(message=message, finish_reason=finish)],
        usage=SimpleNamespace(prompt_tokens=1, completion_tokens=1, total_tokens=total))


def _wants(*calls):
    return _response("好，我做", calls, finish="tool_calls")


def _loop(name="ok", n=20):
    """一直叫同一支工具的劇本，用來把各種預算撞到底。"""
    return [_wants((f"c{i}", name, "{}")) for i in range(n)]


class FakeBot(LLM):
    """真的 LLM，只是 ask() 不出門，照劇本吐真的 Reply。history 照樣長出來。"""

    def __init__(self, *script):
        super().__init__()
        self.script = list(script)
        self.asked = []  # [(prompt, tool_results)]，驗「話真的送到了」用

    def ask(self, prompt=None, images=None, tool_results=None, **kw):
        self.asked.append((prompt, tool_results))
        for call_id, out in (tool_results or {}).items():
            self.history.append({"role": "tool", "tool_call_id": call_id, "content": out})
        if prompt:
            self.history.append({"role": "user", "content": prompt})
        if not self.script:
            return Reply(None, err=RuntimeError("劇本用完了"))
        item = self.script.pop(0)
        if isinstance(item, Exception):
            return Reply(None, err=item)
        return Reply(item, self, remember=True)


def go(bot, dispatch=None, prompt="做事", **kw):
    """跑完一整圈，回那個 Handle。同步用法就是這一行。"""
    return asyncio.run(agentloop.run(bot, dispatch if dispatch is not None else TOOLS,
                                     prompt, **kw))


ran = []
TOOLS = {
    "ok": lambda: ran.append("ok") or "做完了",
    "boom": lambda: 1 / 0,
    "one_arg": lambda x: f"拿到 {x}",
    "huge": lambda: "x" * (agentloop.MAX_OUTPUT + 500),
    "nothing": lambda: None,
    "slow": lambda: time.sleep(0.12) or "慢慢做完了",
}


def basics():
    print("\n== 一來一回 ==")
    h = go(FakeBot(_response("不用工具，直接答")))
    _check("不叫工具就收工", f"{h.stop} {h.round} {h.text}", "done 1 不用工具")

    h = go(FakeBot(_wants(("a", "ok", "{}")), _response("做完了，報告完畢")))
    _check("叫了工具就跑，結果餵回去", f"{h.stop} {h.round} {h.steps[0][3]}", "done 2 做完了")
    _check("模型最後那句話留著", h.text, "報告完畢")
    _check("token 有累加", str(h.tokens), "20")

    bot = FakeBot(_wants(("a", "ok", "{}"), ("b", "one_arg", '{"x": 7}')),
                  _response("兩個都收到了"))
    h = go(bot)
    _check("一輪兩個工具都跑，一個都不漏", str(sorted(bot.asked[1][1])), "['a', 'b']")
    _check("結果對得上 call id", h.steps[1][3], "拿到 7")


def broken():
    print("\n== 工具壞掉不會打斷迴圈 ==")
    def only(*calls):
        return go(FakeBot(_wants(*calls), _response("知道了"))).steps[0][3]

    _check("沒這個工具", only(("a", "nope", "{}")), "no such tool: nope")
    _check("參數對不上", only(("a", "one_arg", '{"y": 1}')), "cannot take these arguments")
    _check("工具自己炸了", only(("a", "boom", "{}")), "boom failed: ZeroDivisionError")
    _check("args 不是合法 JSON", only(("a", "ok", "{壞掉")), "not valid JSON")
    _check("回 None 也講清楚", only(("a", "nothing", "{}")), "(no output)")
    _check("太長就截", only(("a", "huge", "{}")), "truncated, 500 more")


def budgets():
    print("\n== 預算：撞到就停 ==")
    h = go(FakeBot(*_loop()), limits=Limits(rounds=3))
    _check("輪數用完", f"{h.stop} {h.round}", "budget 3")

    h = go(FakeBot(*_loop()), limits=Limits(calls=2))
    _check("工具總次數用完", f"{h.stop} {h.calls}", "calls 2")

    h = go(FakeBot(*_loop()), limits=Limits(tokens=25))
    _check("token 用完", f"{h.stop} {h.tokens}", "tokens 30")

    h = go(FakeBot(*_loop("slow")), limits=Limits(seconds=0.2))
    _check("時間用完", f"{h.stop} {h.elapsed() > 0.2}", "time True")

    h = go(FakeBot(_response("話還沒講完就被切", finish="length")))
    _check("被 max_tokens 切斷不算講完", h.stop, "length")

    h = go(FakeBot(RuntimeError("端點壞了")))
    _check("llms 回錯就停", f"{h.stop} {h.err}", "error 端點壞了")


def allowed():
    print("\n== 預算：這支工具現在不給用（回一句話，不停整個 agent）==")
    ran.clear()
    h = go(FakeBot(*_loop()), limits=Limits(per_tool={"ok": 2}, rounds=6))
    _check("指定工具用滿就擋", h.steps[-1][3], "used 2 times, which is its limit")
    _check("擋掉的不算進用量", f"{h.used} {len(ran)}", "{'ok': 2} 2")

    h = go(FakeBot(*_loop()), limits=Limits(tools=["one_arg"], rounds=2))
    _check("不在白名單就擋", h.steps[0][3], "not available for this task")
    _check("白名單擋掉的一樣有回話", str(len(h.steps[0][3]) > 0), "True")

    bot = FakeBot(*_loop())
    bot.engine.model = "lm-qwen3.5-9b"
    h = go(bot, limits=Limits(engines=["deepseek-chat"]))
    _check("引擎不在清單裡就停", f"{h.stop} {h.err}", "engine 引擎 lm-qwen3.5-9b 不在")


def quiet():
    print("\n== 預算：連續不叫工具 ==")
    bot = FakeBot(_response("我覺得應該要改那個檔"), _wants(("a", "ok", "{}")),
                  *[_response("做完了") for _ in range(3)])
    h = go(bot, limits=Limits(quiet=3))
    _check("不叫工具會被推一把", str(bot.asked[1][0]), "用工具動手")
    _check("推完就動手了", f"{h.stop} {h.calls}", "done 1")
    # quiet 調大的代價：它**真的**講完的那次也會被多推兩下才收工
    _check("講完了也照推，多燒兩輪", f"{h.round} {h.quiet}", "5 3")

    h = go(FakeBot(*[_response(f"第 {i} 次講廢話") for i in range(5)]),
           limits=Limits(quiet=3))
    _check("推不動就放棄", f"{h.stop} {h.round} {h.quiet}", "done 3 3")


async def _steering():
    """跑到一半從外面問狀況、插話、暫停、放行 —— 這是這一包真正的用法。"""
    h, seen = agentloop.Handle(), []

    async def peek():  # 工具在 thread 裡跑，event loop 不該被它卡住
        while not h.done():
            seen.append(h.now())
            await asyncio.sleep(0.005)

    def wait():
        time.sleep(0.05)
        h.say("順便看一下 b.txt")
        h.pause()
        return "看完了"

    bot = FakeBot(_wants(("a", "wait", "{}")), _wants(("b", "ok", "{}")),
                  _response("好，收工"))
    watcher = asyncio.create_task(peek())
    task = asyncio.create_task(agentloop.run(bot, {**TOOLS, "wait": wait}, "做事", h))
    while h.phase != "paused":
        await asyncio.sleep(0.005)
    paused = h.now()
    h.resume()
    await task
    await watcher
    return h, bot, seen, paused


def steering():
    print("\n== 一邊跑一邊問、一邊控制 ==")
    h, bot, seen, paused = asyncio.run(_steering())
    _check("跑工具的時候問得到在跑哪個", " ".join(seen), "正在跑 wait")
    _check("暫停中看得出來", paused, "暫停中")
    _check("插的話真的送到模型那邊", str(bot.asked[1][0]), "順便看一下 b.txt")
    _check("放行之後跑完", f"{h.stop} {h.round}", "done 3")

    handle = agentloop.Handle()
    tools = {**TOOLS, "ok": lambda: handle.ask_stop() or "順便喊停"}
    h = go(FakeBot(*_loop()), tools, handle=handle)
    _check("外面喊停就收手", f"{h.stop} {h.round}", "stopped 2")


def resuming():
    print("\n== 停在一半，接著跑 ==")
    ran.clear()
    bot = FakeBot(_wants(("a", "ok", "{}")))
    h = go(bot, limits=Limits(rounds=1))
    _check("預算用完時那批工具還沒跑", f"{h.stop} {len(ran)}", "budget 0")
    _check("債留在 history 上", str([c["name"] for c in bot.pending_calls]), "['ok']")

    bot.script.append(_response("這次做完了"))
    h = go(bot, prompt=None)
    _check("同一個 bot 再叫一次就接著跑", f"{h.stop} {len(ran)} {h.text}", "done 1 這次做完了")


def real(model):
    """真的接上模型 + base_tools，看這個迴圈自己跑得完一件事。"""
    import tempfile
    import base_tools
    from llms import Engine

    print(f"\n== 交給 {model} 真的跑一次 ==")
    with tempfile.TemporaryDirectory() as tmp:
        base_tools.set_root(tmp)
        base_tools.write_file("notes.txt", "蘋果 3\n香蕉 5\n橘子 2\n")
        schemas, dispatch = base_tools.tools()
        bot = LLM(engine=Engine(model=model), tools=schemas,
                  system="用繁體中文回答。要看檔案或改檔案就用工具，不要用猜的。")
        h = go(bot, dispatch, "notes.txt 裡的水果加起來幾個？答案寫進 total.txt。",
               limits=Limits(rounds=8, per_tool={"run_shell": 3}))
        for round_no, name, args, out in h.steps:
            print(f"  [{round_no}] {name}({args}) -> {out.splitlines()[0][:60]}")
        print("  ", h.now())
        _check("跑完了", str(h.stop), "done")
        _check("檔案真的寫出來了", base_tools.read_file("total.txt"), lambda s: "10" in s)


def main():
    print("agentloop 離線煙霧測試（假 bot，真 Reply）")
    basics()
    broken()
    budgets()
    allowed()
    quiet()
    steering()
    resuming()
    if len(sys.argv) > 1:
        real(sys.argv[1])
    print("\n全部通過" if not FAILED else f"\n沒過的關: {FAILED}")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
