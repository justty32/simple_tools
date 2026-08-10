"""基本 loop、tool failure、budgets 與 quiet 的離線關卡。"""

from agentloop import Limits

from ._testing import FakeBot, TOOLS, check, go, loop, ran, response, wants


def basics():
    print("\n== 一來一回 ==")
    h = go(FakeBot(response("不用工具，直接答")))
    check("不叫工具就收工", f"{h.stop} {h.step} {h.text}", "done 1 不用工具")
    h = go(FakeBot(wants(("a", "ok", "{}")), response("做完了，報告完畢")))
    check("叫了工具就跑，結果餵回去", f"{h.stop} {h.step} {h.tool_log[0][3]}", "done 2 做完了")
    check("模型最後那句話留著", h.text, "報告完畢")
    check("token 有累加", str(h.tokens), "20")
    bot = FakeBot(wants(("a", "ok", "{}"), ("b", "one_arg", '{"x": 7}')),
                  response("兩個都收到了"))
    h = go(bot)
    check("一步兩個工具都跑，一個都不漏", str(sorted(bot.asked[1][1])), "['a', 'b']")
    check("結果對得上 call id", h.tool_log[1][3], "拿到 7")


def broken():
    print("\n== 工具壞掉不會打斷迴圈 ==")
    def only(*calls):
        return go(FakeBot(wants(*calls), response("知道了"))).tool_log[0][3]
    check("沒這個工具", only(("a", "nope", "{}")), "no such tool: nope")
    check("參數對不上", only(("a", "one_arg", '{"y": 1}')), "cannot take these arguments")
    check("工具自己炸了", only(("a", "boom", "{}")), "boom failed: ZeroDivisionError")
    check("args 不是合法 JSON", only(("a", "ok", "{壞掉")), "not valid JSON")
    check("回 None 也講清楚", only(("a", "nothing", "{}")), "(no output)")
    check("太長就截", only(("a", "huge", "{}")), "truncated, 500 more")


def budgets():
    print("\n== 預算：撞到就停 ==")
    for label, limits, want in [
        ("步數用完", Limits(steps=3), "budget 3"),
        ("工具總次數用完", Limits(calls=2), "calls 2"),
        ("token 用完", Limits(tokens=25), "tokens 30"),
    ]:
        h = go(FakeBot(*loop()), limits=limits)
        value = f"{h.stop} {h.step if h.stop == 'budget' else h.calls if h.stop == 'calls' else h.tokens}"
        check(label, value, want)
    h = go(FakeBot(*loop("slow")), limits=Limits(seconds=0.2))
    check("時間用完", f"{h.stop} {h.elapsed() > 0.2}", "time True")
    h = go(FakeBot(response("話還沒講完就被切", finish="length")))
    check("被 max_tokens 切斷不算講完", h.stop, "length")
    h = go(FakeBot(RuntimeError("端點壞了")))
    check("llms 回錯就停", f"{h.stop} {h.err}", "error 端點壞了")


def policy_and_quiet():
    print("\n== 預算：這支工具現在不給用 ==")
    ran.clear()
    h = go(FakeBot(*loop()), limits=Limits(per_tool={"ok": 2}, steps=6))
    check("指定工具用滿就擋", h.tool_log[-1][3], "used 2 times, which is its limit")
    check("擋掉的不算進用量", f"{h.used} {len(ran)}", "{'ok': 2} 2")
    h = go(FakeBot(*loop()), limits=Limits(tools=["one_arg"], steps=2))
    check("不在白名單就擋", h.tool_log[0][3], "not available for this task")
    check("白名單擋掉的一樣有回話", str(bool(h.tool_log[0][3])), "True")
    bot = FakeBot(*loop()); bot.engine.model = "lm-qwen3.5-9b"
    h = go(bot, limits=Limits(engines=["deepseek-chat"]))
    check("引擎不在清單裡就停", f"{h.stop} {h.err}", "engine 引擎 lm-qwen3.5-9b 不在")

    print("\n== 預算：連續不叫工具 ==")
    bot = FakeBot(response("我覺得應該要改"), wants(("a", "ok", "{}")),
                  *[response("做完了") for _ in range(3)])
    h = go(bot, limits=Limits(quiet=3))
    check("不叫工具會被推一把", str(bot.asked[1][0]), "用工具動手")
    check("推完就動手了", f"{h.stop} {h.calls}", "done 1")
    check("講完了也照推，多燒兩步", f"{h.step} {h.quiet}", "5 3")
    h = go(FakeBot(*[response(f"第 {i} 次講廢話") for i in range(5)]), limits=Limits(quiet=3))
    check("推不動就放棄", f"{h.stop} {h.step} {h.quiet}", "done 3 3")
