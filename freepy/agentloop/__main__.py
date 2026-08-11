"""`python -m agentloop`：離線關卡，可選再加一關真模型。"""

import sys
import tempfile

from agentloop import Handle
from agentloop.limits import Limits

from ._checks_contracts import contracts
from ._checks_core import basics, broken, budgets, policies
from ._checks_live import resuming, steering
from ._checks_inputs import dynamic_inputs, step_commit
from ._checks_races import races
from ._checks_stop import stop_boundaries
from ._testing import FAILED, check, go


def real(model):
    import base_tools
    from llms import Engine, LLM

    print(f"\n== 交給 {model} 真的跑一次 ==")
    with tempfile.TemporaryDirectory() as tmp:
        base_tools.set_root(tmp)
        base_tools.write_file("notes.txt", "蘋果 3\n香蕉 5\n橘子 2\n")
        schemas, dispatch = base_tools.tools()
        bot = LLM(engine=Engine(model=model), tools=schemas,
                  system="用繁體中文回答。要看檔案或改檔案就用工具，不要用猜的。")
        handle = Handle()
        Limits(steps=8, per_tool={"run_shell": 3}).attach(handle)
        h = go(bot, dispatch, "notes.txt 裡的水果加起來幾個？答案寫進 total.txt。",
               handle=handle)
        for step_no, name, args, out in h.tool_log:
            print(f"  [{step_no}] {name}({args}) -> {out.splitlines()[0][:60]}")
        print("  ", h.now())
        check("跑完了", str(h.stop), "done")
        check("檔案真的寫出來了", base_tools.read_file("total.txt"), lambda s: "10" in s)


def main():
    print("agentloop 離線驗證（假 bot，真 Reply）")
    basics()
    broken()
    budgets()
    policies()
    steering()
    resuming()
    print("\n== 競態與控制邊界 ==")
    races()
    dynamic_inputs()
    step_commit()
    stop_boundaries()
    contracts()
    if len(sys.argv) > 1:
        real(sys.argv[1])
    print("\n全部通過" if not FAILED else f"\n沒過的關: {FAILED}")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
