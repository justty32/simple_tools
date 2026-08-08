"""python -m exec_tools — 離線煙霧測試，完全不碰 LLM。

在臨時資料夾裡放幾個假工具連 spec 一起寫出來，走完整條路：掃描 → 組 argv →
跑 → 收字串。全部在本機、幾毫秒、不用 proxy。
"""

import json
import os
import sys
import tempfile

from . import args as argmod
from . import discover, spec, tools

TOOLS = {
    # 正常解析參數的工具：驗 positional / option / switch / repeat / separate
    "greet": """#!/bin/sh
loud=; note=; who=; tags=
while [ $# -gt 0 ]; do
  case "$1" in
    --loud) loud=1 ;;
    --note=*) note="${1#--note=}" ;;
    --note) shift; note="$1" ;;
    --tag) shift; tags="$tags[$1]" ;;
    *) who="$1" ;;
  esac
  shift
done
if [ -n "$loud" ]; then echo "HELLO $who"; else echo "hello $who"; fi
[ -n "$note" ] && echo "extra=$note"
[ -n "$tags" ] && echo "tags=$tags"
if [ "$who" = "boom" ]; then echo "something broke" >&2; exit 3; fi
exit 0
""",
    "count": '#!/bin/sh\necho "chars=$(wc -c)"\n',
    # 沒找到就 exit 1，但那不是失敗 —— 驗 ok_exit
    "seek": '#!/bin/sh\ncase "$1" in *x*) echo found ;; *) exit 1 ;; esac\n',
    "binary": '#!/bin/sh\nprintf \'ok\\000\\001\\002binary\'\n',
    "noisy": '#!/bin/sh\ni=1\nwhile [ $i -le 60 ]; do echo "line $i"; i=$((i+1)); done\n',
}


def _spec(root, name, argv=None, stdin=None, required=None, **extra):
    props = {p: {"type": b.pop("_type", "string")} for p, b in (argv or {}).items()}
    if stdin:
        props[stdin] = {"type": "string"}
    return {
        "type": "function",
        "function": {"name": name, "description": f"假的 {name}",
                     "parameters": {"type": "object", "properties": props,
                                    "required": required or []}},
        # exec 是 "../name" 不是 "./name"：相對路徑的中心是這份 .json，而它住在 .specs/ 裡
        "_extra": {"_version": spec.VERSION, "_type": "exec", "exec": [f"../{name}"],
                   "argv": argv or {}, "stdin": {"param": stdin} if stdin else None,
                   "timeout": 5, "source": spec.fingerprint(os.path.join(root, name)),
                   **extra},
    }


def _write(root):
    for name, body in TOOLS.items():
        path = os.path.join(root, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(body)
        os.chmod(path, 0o755)

    def put(name, **kw):
        spec.save(_spec(root, name, **kw), discover.spec_path(os.path.join(root, name)))

    put("greet",
        argv={"who": {"position": 1},
              "loud": {"position": 2, "flag": "--loud", "_type": "boolean"},
              "note": {"position": 3, "flag": "--note"},
              "tag": {"position": 4, "flag": "--tag", "repeat": True}},
        limits={"who": {"max_bytes": 20}})
    put("count", stdin="text")
    put("seek", argv={"q": {"position": 1}}, required=["q"], ok_exit=[0, 1])
    # 一個檔案裝兩個 tool，而且檔名跟裡面的 tool 名字都對不上 —— 掃描不該靠檔名
    spec.save([_spec(root, "binary"), _spec(root, "noisy", stdout={"clip": "tail"})],
              os.path.join(root, discover.SPEC_DIR, "bundle.json"))


def _check(label, got, want):
    ok = want in got if isinstance(want, str) else want(got)
    print(f"  {'ok  ' if ok else 'FAIL'} {label}: {got[:70]!r}")
    return ok


def main():
    root = tempfile.mkdtemp(prefix="exec_tools_")
    _write(root)
    os.environ[discover.ENV] = root
    specs, missing, errors = discover.scan()
    print(f"掃描 {root}\n  找到 {sorted(specs)}，沒 spec 的 {missing}，壞掉的 {errors}")

    schemas, d = tools()
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
        _check("一個檔案兩個 tool", str(sorted(specs)), lambda s: "binary" in s and "noisy" in s),
        _check("二進位不吐亂碼", d["binary"](), "binary output"),
        _check("clip tail 留尾巴", _trunc(specs["noisy"]),
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
    ]
    print(f"\n{sum(results)}/{len(results)} 過")
    return 0 if all(results) else 1


def _trunc(one):
    """輸出沒長到 MAX_OUTPUT，所以拿 60 的上限直接驗一次 clip 的方向。"""
    from .invoke import _clip, run
    return _clip(run(one, {}), one.body.clip, 60)


def _tweak(one):
    one.body.order = [(p, {**b, "separate": False} if p == "note" else b)
                      for p, b in one.body.order]
    return one


if __name__ == "__main__":
    sys.exit(main())
