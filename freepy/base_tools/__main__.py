"""__main__.py — 自己跑一遍確認四個工具都還活著。

    python -m base_tools                  # 不碰網路，只驗工具本身
    python -m base_tools deepseek-chat    # 再多一關：真的讓模型用這些工具做事

跑在臨時資料夾裡，不會動到你的檔案。
"""

import os
import sys
import tempfile

import base_tools
from base_tools import edit_file, read_file, run_shell, write_file

URL = "http://localhost:4000"

FAILED = []


def check(label, got, want_in):
    """want_in 要出現在 got 裡面才算過（給 callable 就自己判），印一行結果。"""
    ok = want_in in got if isinstance(want_in, str) else want_in(got)
    if not ok:
        FAILED.append(label)
    print(f"[{'ok' if ok else 'FAIL'}] {label}: {got.splitlines()[0][:70] if got else '(空)'}")


def demo_files():
    print("== 讀寫 ==")
    check("寫新檔", write_file("a/b.txt", "one\ntwo\nthree\n"), "Created")
    check("覆寫", write_file("a/b.txt", "one\ntwo\nthree\n"), "Overwrote")
    check("讀回來有行號", read_file("a/b.txt"), "     1\tone")
    check("offset/limit", read_file("a/b.txt", offset=2, limit=1), "     2\ttwo")
    check("空檔案講清楚", write_file("empty.txt", "") and read_file("empty.txt"), "is empty")
    check("找不到檔", read_file("nope.txt"), "file not found")
    check("讀資料夾", read_file("a"), "is a directory")


def demo_edit():
    print("\n== 編輯 ==")
    write_file("c.txt", "alpha\nbeta\nalpha\n")
    check("換掉唯一一處", edit_file("c.txt", "beta", "BETA"), "Replaced 1")
    check("多處要明講", edit_file("c.txt", "alpha", "x"), "appears 2 times")
    check("明講就換", edit_file("c.txt", "alpha", "x", replace_all=True), "Replaced 2")
    check("對不上的原文", edit_file("c.txt", "nothing here", "y"), "does not appear")
    check("old 不能空", edit_file("c.txt", "", "y"), "non-empty")


def demo_shell():
    print("\n== 指令 ==")
    check("正常執行", run_shell("echo hello"), "hello")
    check("錯誤碼帶回來", run_shell("exit 3"), "exit 3")
    check("stderr 也收", run_shell("ls /definitely-not-here"), "exit")
    check("逾時會停", run_shell("sleep 5", timeout=1), "timed out")
    check("在 root 底下跑", run_shell("pwd"), str(base_tools.get_root()))

    base_tools.set_approver(lambda cmd: False)
    check("守門員擋得住", run_shell("echo nope"), "declined")
    base_tools.set_approver(None)


def demo_root():
    print("\n== 關在 root 裡 ==")
    check("跳出去的相對路徑", read_file("../../etc/passwd"), "outside the workspace")
    check("絕對路徑也擋", read_file("/etc/passwd"), "outside the workspace")
    check("寫也擋", write_file("/tmp/escaped.txt", "x"), "outside the workspace")


def demo_tooljson():
    """同一組工具，換一條路拿：從 tools.json 讀回來，看它跑不跑得動。"""
    print("\n== 走 tooljson 那條路 ==")
    try:
        import tooljson
    except ModuleNotFoundError:
        print("找不到 tooljson（PYTHONPATH 要有 llmkit），跳過")
        return
    from base_tools import specs

    where = os.path.join(os.path.dirname(os.path.abspath(specs.__file__)), specs.PATH)
    schemas, dispatch = tooljson.tools(where)
    check("四個都讀得回來", str(sorted(dispatch)), "'edit_file', 'read_file'")
    check("schema 剝乾淨了", str(schemas[0]), lambda s: "_extra" not in s)
    check("讀回來的真的叫得動", dispatch["write_file"](path="j.txt", content="hi\n"), "Created")
    check("root 照樣關得住", dispatch["read_file"](path="/etc/passwd"), "outside the workspace")
    check("宣告沒跟實作走散", str(tooljson.load_all(where)[0].stale), "False")


def demo_agent(model):
    """真的接上模型：叫它用工具做一件小事，看它會不會用。"""
    print(f"\n== 交給 {model} 用 ==")
    from llms import LLM, Engine

    schemas, dispatch = base_tools.tools()
    bot = LLM(engine=Engine(url=URL, model=model), tools=schemas,
              system="你是一個會用工具處理檔案的助手，動手做，不要只回答。")
    if bot.engine.supports("tools") is False:
        print("這個模型宣告不支援 tool calling，跳過")
        return

    reply = bot.ask(
        "工作目錄裡有一個 a/b.txt，把裡面的 two 改成 TWO，然後把改完的內容讀出來給我看。"
    )
    for _ in range(8):  # 給它幾回合，別讓壞掉的模型無限打轉
        if not reply or not reply.calls:
            break
        for c in reply.calls:
            print(f"  -> {c['name']}({c['args']})")
        results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in reply.calls}
        reply = bot.ask(tool_results=results)

    print("回答:", reply.text, "err:", reply.err)
    check("模型真的改到檔", read_file("a/b.txt"), "TWO")


def main():
    model = sys.argv[1] if len(sys.argv) > 1 else None
    with tempfile.TemporaryDirectory() as tmp:
        print("工作根目錄:", base_tools.set_root(tmp))
        demo_files()
        demo_edit()
        demo_shell()
        demo_root()
        demo_tooljson()
        if model:
            demo_agent(model)
    print("\n全部通過" if not FAILED else f"\n沒過的關: {FAILED}")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
