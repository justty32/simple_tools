"""python -m tooljson — 離線煙霧測試，完全不碰 LLM。

素材是 `examples.build()` 寫進臨時資料夾的五個假工具，走完整條路：讀 .json →
組 argv → 跑 → 收字串。全部在本機、幾毫秒、不用 proxy、不用 openai。

最後三關驗的是**第三方登記自己的 `_type`**：內建的 `exec` 沒有特權，走的是同一道門。
"""

import json
import os
import sys
import tempfile

from . import args as argmod
from . import examples, invoke, register, spec, tools


class Echo:
    """第三方 `_type` 的最小形狀：一個 `run()`、一個 `target`，其餘自己說了算。"""

    target = None                       # 沒有對應的本地檔案，所以 stale 永遠是 None

    def __init__(self, one):
        self.word = one.extra.get("word", "?")

    def run(self, args):
        return f"{self.word} {args.get('x', '')}".strip()


ECHO_SPEC = {
    "type": "function",
    "function": {"name": "echo", "description": "第三方登記的 _type",
                 "parameters": {"type": "object",
                                "properties": {"x": {"type": "string"}}}},
    "_extra": {"_version": spec.VERSION, "_type": "echo", "word": "說"},
}


def _check(label, got, want):
    ok = want in got if isinstance(want, str) else want(got)
    print(f"  {'ok  ' if ok else 'FAIL'} {label}: {got[:70]!r}")
    return ok


def _fails(data):
    """讀一份注定壞掉的 spec，回它的錯誤訊息。沒壞掉才是問題。"""
    try:
        return f"沒有拒絕：{spec.Spec(data).name}"
    except spec.SpecError as e:
        return str(e)


def main():
    root = tempfile.mkdtemp(prefix="tooljson_")
    paths = examples.build(root)
    schemas, d = tools(*paths)
    specs = {one.name: one for p in paths for one in spec.load_all(p)}
    print(f"讀 {root}/{examples.SPECS}：{len(paths)} 個檔案，{sorted(d)}")
    results = [
        _check("schema 剝乾淨了", json.dumps(schemas[0], ensure_ascii=False),
               lambda s: "_extra" not in s and '"type": "function"' in s),
        _check("positional", d["greet"](who="world"), "hello world"),
        _check("switch 有給", d["greet"](who="w", loud=True), "HELLO w"),
        _check("switch 假值連旗標都不放", d["greet"](who="w", loud=False),
               lambda s: s.strip() == "hello w"),
        _check("option 有 flag", d["greet"](who="a", note="n"), "extra=n"),
        _check("repeat", d["greet"](who="a", tag=["x", "y"]), "tags=[x][y]"),
        _check("沒 repeat 就不收 list", d["greet"](who="a", note=["x"]), "does not take a list"),
        _check("stdin", d["count"](text="12345"), "chars=5"),
        _check("ok_exit 內的 exit 1 找得到就正常回", d["seek"](q="axc"), "found"),
        # 宣告過的結束碼不會被包成失敗的 "exit N\n..." 形式，模型不會以為工具壞了
        _check("ok_exit 內的非 0 不算失敗", d["seek"](q="abc"),
               lambda s: not s.startswith("exit ")),
        _check("ok_exit 外的非 0 帶 exit N", d["greet"](who="boom"), "exit 3"),
        _check("stderr 真的併進來", d["greet"](who="boom"), "something broke"),
        _check("一個檔案兩個 tool", str(sorted(d)), lambda s: "binary" in s and "noisy" in s),
        _check("二進位不吐亂碼", d["binary"](), "binary output"),
        # noisy 的輸出沒長到 MAX_OUTPUT，所以拿 60 的上限直接驗一次 clip 的方向
        _check("clip tail 留尾巴", invoke._clip(specs["noisy"].run({}), "tail", 60),
               lambda s: "line 60" in s and "line 1\n" not in s),
        _check("required 漏了", d["seek"](), "missing required"),
        _check("多給不認得的", d["greet"](who="a", zzz=1), "unknown argument"),
        _check("limits 擋下來", d["greet"](who="x" * 30), "over the 20 limit"),
        _check("型別轉得動", d["greet"](who="a", loud="true"), "HELLO a"),
        _check("型別轉不動", d["greet"](who="a", loud="maybe"), "wrong type"),
        _check("值不會被 shell 重新解析", d["greet"](who="a; rm -rf /"), "hello a; rm -rf /"),
        _check("spec 沒過期", str(specs["greet"].stale), "False"),
        # 順序照 position，不照 key 的順序，也不照字母
        _check("argv 順序照 position", str(argmod.build(
            specs["greet"], {"tag": ["t"], "loud": True, "who": "w"})[0][1:]),
            lambda s: s == "['w', '--loud', '--tag', 't']"),
        _check("separate false 用等號", str(argmod.build(
            _tweak(specs["greet"]), {"who": "w", "note": "n"})[0][1:]),
            lambda s: s == "['w', '--note=n']"),
        # 登記之前先確認真的不認得，免得下一關是因為別的原因才過
        _check("沒登記的 _type 被拒絕", _fails(ECHO_SPEC), "目前登記的只有"),
    ]

    register("echo", Echo)
    where = os.path.join(root, "echo.json")
    spec.save(ECHO_SPEC, where)
    results += [
        _check("登記完就叫得動", spec.load(where).run({"x": "hi"}), "說 hi"),
        _check("第三方的也進得了 dispatch", str(sorted(tools(*paths, where)[1])),
               lambda s: "echo" in s),
    ]
    print(f"\n{sum(results)}/{len(results)} 過")
    return 0 if all(results) else 1


def _tweak(one):
    """separate 沒有專屬的假工具，直接把 greet 的 note 改成等號式再組一次 argv。"""
    one.body.order = [(p, {**b, "separate": False} if p == "note" else b)
                      for p, b in one.body.order]
    return one


if __name__ == "__main__":
    sys.exit(main())
