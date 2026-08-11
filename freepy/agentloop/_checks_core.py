"""Core Step/tool loop, callback mutation, failures and optional policies."""

from agentloop import END, Handle
from agentloop.limits import Limits

from ._testing import FakeBot, TOOLS, check, go, loop, ran, response, wants


def basics():
    print("\n== Step → callbacks → tools → callbacks ==")
    h = go(FakeBot(response("不用工具，直接答")))
    check("不叫工具就依 auto_finish 收工", f"{h.stop} {h.step} {h.text}",
          "done 1 不用工具")

    seen = []
    handle_bot = FakeBot(wants(("a", "one_arg", '{"x": 1}')), response("完成"))
    handle = Handle()

    def rewrite_call(h):
        seen.append(("step", h.step, h.tool_calls[0]["args"]["x"]))
        h.tool_calls[0]["args"]["x"] = 9

    def rewrite_result(h):
        seen.append(("tools", h.calls, h.tool_results["a"]))
        h.tool_results["a"] = "callback 改過"

    handle.after_step.append(rewrite_call)
    handle.after_tools.append(rewrite_result)
    h = go(handle_bot, handle=handle)
    check("after_step 可改模型要求的 call", h.tool_log[0][3], "拿到 9")
    check("after_tools 改過的結果才送入下步", str(handle_bot.asked[1][1]),
          "callback 改過")
    check("兩種 callback 看得到已提交狀態", repr(seen),
          "[('step', 1, 1), ('tools', 1, '拿到 9')")

    called = []
    handle = Handle()
    handle.after_step.extend([
        lambda h: called.append("first") or END,
        lambda h: called.append("second"),
    ])
    h = go(FakeBot(response("停")), handle=handle)
    check("END 不會短路同批其他 callbacks", f"{called} {h.stop}",
          "['first', 'second'] ended")


def broken():
    print("\n== 工具壞掉仍成為可修改的 result ==")

    def only(*calls):
        return go(FakeBot(wants(*calls), response("知道了"))).tool_log[0][3]

    check("沒這個工具", only(("a", "nope", "{}")), "no such tool: nope")
    check("參數對不上", only(("a", "one_arg", '{"y": 1}')),
          "cannot take these arguments")
    check("工具自己炸了", only(("a", "boom", "{}")),
          "boom failed: ZeroDivisionError")
    check("args 不是合法 JSON", only(("a", "ok", "{壞掉")), "not valid JSON")
    check("回 None 也講清楚", only(("a", "nothing", "{}")), "(no output)")
    check("太長就截", only(("a", "huge", "{}")), "truncated, 500 more")


def budgets():
    print("\n== Limits 是 callback policy ==")
    h = limited(FakeBot(*loop()), Limits(steps=3))
    check("步數用完", f"{h.stop} {h.step}", "budget 3")
    h = limited(FakeBot(*loop()), Limits(calls=2))
    check("工具總次數用完", f"{h.stop} {h.calls}", "calls 2")
    h = limited(FakeBot(*loop()), Limits(input_tokens=25))
    check("input token 用完", f"{h.stop} {h.input_tokens}",
          "input_tokens 27")
    h = limited(FakeBot(*loop()), Limits(output_tokens=3))
    check("output token 用完", f"{h.stop} {h.output_tokens}",
          "output_tokens 3")
    h = go(FakeBot(response("完成", total=12, prompt=9, completion=3, cached=4)))
    check("用量分開累計，total 只是報表",
          f"{h.input_tokens}/{h.output_tokens}/{h.cached_input_tokens}/{h.tokens}",
          "9/3/4/12")
    h = limited(FakeBot(*loop("slow")), Limits(seconds=0.2))
    check("時間用完", f"{h.stop} {h.elapsed() > 0.2}", "time True")
    h = go(FakeBot(response("話還沒講完", finish="length")))
    check("length 不算正常完成", h.stop, "length")
    h = go(FakeBot(RuntimeError("端點壞了")))
    check("端點錯誤", f"{h.stop} {h.err}", "error 端點壞了")


def policies():
    print("\n== Limits 在 after_step 修改 calls/results ==")
    ran.clear()
    bot = FakeBot(wants(("a", "ok", "{}")), wants(("b", "ok", "{}")),
                  wants(("c", "ok", "{}")), response("收到限制"))
    h = limited(bot, Limits(per_tool={"ok": 2}))
    check("指定工具用滿後改成錯誤 result", str(bot.asked[-1][1]),
          "used 2 times")
    check("被擋掉的不算執行", f"{h.used} {len(ran)}", "{'ok': 2} 2")
    h = limited(FakeBot(wants(("a", "ok", "{}")), response("收到限制")),
                Limits(tools=["one_arg"]))
    check("白名單在執行前移除 call", str(h.calls), "0")
    check("白名單仍回 result 給模型", str(h.bot.asked[1][1]), "not available")
    bot = FakeBot(response("不該准用"))
    bot.engine.model = "lm-qwen3.5-9b"
    h = limited(bot, Limits(engines=["deepseek-chat"]))
    check("引擎 policy 由 callback 結束", f"{h.stop} {h.err}",
          "engine 引擎 lm-qwen3.5-9b 不在")


def limited(bot, limits):
    handle = Handle()
    limits.attach(handle)
    return go(bot, handle=handle)
