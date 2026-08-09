"""python -m tooljson — 離線煙霧測試，完全不碰 LLM。

素材是 `examples.build()` 寫進臨時資料夾的五個假工具，走完整條路：讀 .json →
組 argv → 跑 → 收字串。全部在本機、幾毫秒、不用 proxy、不用 openai。

中段驗的是**第三方登記自己的 `_type`**：內建的 `exec` 沒有特權，走的是同一道門。
最後一段驗 `_type: "python"`：宣告一個 Tool → 存成 .json → 讀回來 → 叫得動。
"""

import json
import os
import subprocess
import sys
import tempfile

import tooljson

from . import args as argmod
from . import examples, from_tool, register, spec, text, tools


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


PY_SOURCE = '''
import tooljson


class Shout(tooljson.Tool):
    """宣告式的工具：schema 自己寫，不靠反射。"""

    name = "shout"
    description = "把字變大聲"
    params = {"text": {"type": "string", "description": "要喊的字"},
              "times": {"type": "integer", "description": "重複幾次"}}
    required = ["text"]

    def run(self, text, times=1):
        return " ".join([text.upper()] * times)


class Counter(tooljson.Tool):
    """回非字串，驗 as_text；順便驗兩次呼叫之間不留狀態。"""

    n = 0

    def run(self, **kw):
        Counter.n += 1
        return {"call": Counter.n, "kw": kw}


def plain(x="?"):
    """一般函式也叫得動，不是非得繼承 Tool。"""
    return f"plain {x}"


def boom():
    raise RuntimeError("炸了")


if __name__ == "__main__":
    # 這一段就是使用者會寫的 `python weather.py`：__module__ 這時候是 "__main__"
    import json
    print(json.dumps(tooljson.from_tool(Shout)["_extra"], ensure_ascii=False))
'''


def _py_specs(root):
    """把模組寫到磁碟，回三份指到它的 spec 路徑（檔案、資料夾、純 module 名各一）。"""
    pkg = os.path.join(root, "pytools")
    os.makedirs(pkg, exist_ok=True)
    with open(os.path.join(pkg, "mytools.py"), "w", encoding="utf-8") as f:
        f.write(PY_SOURCE)
    return pkg


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
        _check("clip tail 留尾巴", text.clip(specs["noisy"].run({}), "tail", 60),
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
    # ---- _type: "python" ----
    pkg = _py_specs(root)

    # 從**真的住在那個檔案裡**的 class 產 spec，才驗得到 source 指紋那條路
    sys.path.insert(0, pkg)
    import mytools

    made = from_tool(mytools.Shout, module="mytools", path="./pytools/mytools.py")
    shout_at = os.path.join(root, "shout.json")
    spec.save(made, shout_at)

    def _py(name, attr, **extra):
        """一份指到 mytools 的 spec，其餘的鍵各關自己蓋。"""
        where = os.path.join(root, f"py_{name}.json")
        spec.save({"type": "function",
                   "function": {"name": name, "parameters": {"type": "object", "properties": {}}},
                   "_extra": {"_version": spec.VERSION, "_type": "python",
                              "module": "mytools", "attr": attr, **extra}}, where)
        return where

    folder = _py("counter", "Counter", path="./pytools")
    plain_at = _py("plain", "plain", path="./pytools")
    boom_at = _py("boom", "boom", path="./pytools")

    results += [
        _check("from_tool 產的 schema 是乾淨的 OpenAI tool",
               json.dumps(made, ensure_ascii=False),
               lambda s: '"name": "shout"' in s and '"_type": "python"' in s),
        _check("存出去讀回來叫得動", spec.load(shout_at).run({"text": "hi"}), "HI"),
        _check("預設值還在", spec.load(shout_at).run({"text": "hi", "times": 2}), "HI HI"),
        _check("指到資料夾也找得到", spec.load(folder).run({}), '"call": 1'),
        _check("每次叫都是乾淨的實體", spec.load(folder).run({"a": 1}), '"kw": {"a": 1}'),
        _check("非字串的回傳值 JSON 化", spec.load(folder).run({}), lambda s: s.startswith("{")),
        _check("一般函式也叫得動", spec.load(plain_at).run({"x": "y"}), "plain y"),
        _check("函式丟例外變成字串不外漏", spec.load(boom_at).run({}), "RuntimeError: 炸了"),
        _check("參數對不上也是字串", spec.load(plain_at).run({"zzz": 1}), "收不下這組參數"),
        _check("python 型也算得出 stale", str(spec.load(shout_at).stale), "False"),
        _check("模組不存在是設定錯，當場丟",
               _fails({**made, "_extra": {**made["_extra"], "module": "nope", "path": None}}),
               "import"),
        _check("attr 不存在也當場丟",
               _fails({**made, "_extra": {**made["_extra"], "attr": "nope",
                                          "path": os.path.join(pkg, "mytools.py")}}),
               "裡沒有 'nope'"),
        _check("兩種內建的 _type 都在", str(tooljson.types()),
               lambda s: "exec" in s and "python" in s),
        # stale 只有在真的翻得動的時候才有意義，所以改一次檔案再問一次
        _check("模組改了就過期", str(_touch(pkg, shout_at)), "True"),
        _check("宣告寫錯當場擋", _bad_tool(), "required 列了 params 裡沒有的"),
        # python weather.py 這樣直接執行時 __module__ 是 "__main__"，別人 import 不到
        _check("直接執行時從檔名反推出模組名", _as_main(pkg), '"module": "mytools"'),
        _check("反推不出來就丟，不猜", _bad_module(), "反推不出"),
        # python -m pkg.mod 的話只看檔名是不夠的，要連 package 一起收
        _check("package 裡的也反推得出完整名字", _as_submodule(root, pkg),
               '"module": "pytools.mytools"'),
    ]

    print(f"\n{sum(results)}/{len(results)} 過")
    return 0 if all(results) else 1


def _touch(pkg, shout_at):
    """在模組尾巴多加一行再讀一次 spec —— source 指紋對不上，stale 要變 True。"""
    with open(os.path.join(pkg, "mytools.py"), "a", encoding="utf-8") as f:
        f.write("\n# 改了一行\n")
    return spec.load(shout_at).stale


def _as_main(pkg):
    """**真的**用 `python mytools.py` 跑一次。

    不能在這支程式裡把 `__module__` 蓋成 `"__main__"` 假裝 —— `inspect.getfile()`
    對 class 是透過 `__module__` 去查檔案的，蓋掉它等於把反推要用的線索也蓋掉，
    測到的會是假的失敗。開一個真的行程才問得到真的答案。
    """
    where = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    done = subprocess.run(
        [sys.executable, os.path.join(pkg, "mytools.py")],
        capture_output=True, text=True,
        env={**os.environ, "PYTHONPATH": where})
    return (done.stdout or done.stderr).strip()


def _as_submodule(root, pkg):
    """把 pytools 變成一個真的 package，再用 `python -m pytools.mytools` 跑一次。

    只看檔名的話會反推成 `"mytools"` —— 那個名字在 `root` 上 import 不到（要
    `pytools.mytools`），而且錯得很安靜：.json 存得出來，等別的行程去讀才炸。
    """
    open(os.path.join(pkg, "__init__.py"), "w").close()
    where = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    done = subprocess.run(
        [sys.executable, "-m", "pytools.mytools"], cwd=root,
        capture_output=True, text=True,
        env={**os.environ, "PYTHONPATH": where})
    return (done.stdout or done.stderr).strip()


def _bad_module():
    """真的住在 __main__.py 裡的 class，檔名反推也還是 __main__，這時候要丟。"""
    try:
        return f"沒有拒絕：{from_tool(_InMain)}"
    except spec.SpecError as e:
        return str(e)


class _InMain(tooljson.Tool):
    """定義在這個檔（__main__.py）裡，所以連檔名都救不了它。"""

    params = {}


def _bad_tool():
    """required 列了 params 裡沒有的東西，schema() 要當場擋下來。"""
    class Bad(tooljson.Tool):
        params = {"a": {"type": "string"}}
        required = ["a", "b"]
    try:
        return f"沒有拒絕：{Bad.schema()}"
    except spec.SpecError as e:
        return str(e)


def _tweak(one):
    """separate 沒有專屬的假工具，直接把 greet 的 note 改成等號式再組一次 argv。"""
    one.body.order = [(p, {**b, "separate": False} if p == "note" else b)
                      for p, b in one.body.order]
    return one


if __name__ == "__main__":
    sys.exit(main())
