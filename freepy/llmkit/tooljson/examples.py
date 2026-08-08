"""examples.py — 一組會動的假工具和它們的 spec。

`python -m tooljson` 拿它當關卡的素材，你也可以直接叫 `build()` 在自己的臨時資料夾
裡生一份出來，開 .json 看規範實際長什麼樣：

    from tooljson import examples
    paths = examples.build("/tmp/demo")     # 回一串 .json 的路徑

五個假工具各自負責證明一件事：

| 工具 | 證明 |
|---|---|
| `greet` | positional / option / switch / repeat / separate、limits |
| `count` | stdin |
| `seek` | `ok_exit`：`grep` 沒找到是 exit 1，那不是失敗 |
| `binary` | 輸出含 NUL 時不吐幾萬個替代字元 |
| `noisy` | `stdout.clip` 的截斷方向 |
"""

import os
from pathlib import Path

from . import spec

#: spec 集中放的子資料夾。純粹是這組範例的擺法，規範不管檔案放哪
SPECS = "specs"

TOOLS = {
    # 正常解析參數的工具，flag 可以出現在任何位置
    "greet": """#!/bin/sh
loud=; note=; who=; tags=
while [ $# -gt 0 ]; do case "$1" in
    --loud) loud=1 ;;                  --note) shift; note="$1" ;;
    --note=*) note="${1#--note=}" ;;   --tag) shift; tags="$tags[$1]" ;;
    *) who="$1" ;;
  esac; shift; done
[ -n "$loud" ] && echo "HELLO $who" || echo "hello $who"
[ -n "$note" ] && echo "extra=$note"; [ -n "$tags" ] && echo "tags=$tags"
[ "$who" = "boom" ] && { echo "something broke" >&2; exit 3; }
exit 0
""",
    "count": '#!/bin/sh\necho "chars=$(wc -c)"\n',
    "seek": '#!/bin/sh\ncase "$1" in *x*) echo found ;; *) exit 1 ;; esac\n',
    "binary": '#!/bin/sh\nprintf \'ok\\000\\001\\002binary\'\n',
    "noisy": '#!/bin/sh\ni=1\nwhile [ $i -le 60 ]; do echo "line $i"; i=$((i+1)); done\n',
}


def one(root, name, argv=None, stdin=None, required=None, **extra):
    """組一份 exec 型的 spec。argv 的值裡可以用 `_type` 指定那個參數的 JSON 型別。"""
    props = {p: {"type": b.pop("_type", "string")} for p, b in (argv or {}).items()}
    props.update({stdin: {"type": "string"}} if stdin else {})
    return {
        "type": "function",
        "function": {"name": name, "description": f"假的 {name}",
                     "parameters": {"type": "object", "properties": props,
                                    "required": required or []}},
        # exec 是 "../name" 不是 "./name"：相對路徑的中心是這份 .json，而它住在 specs/ 裡
        "_extra": {"_version": spec.VERSION, "_type": "exec", "exec": [f"../{name}"],
                   "argv": argv or {}, "stdin": {"param": stdin} if stdin else None,
                   "timeout": 5, "source": spec.fingerprint(os.path.join(root, name)),
                   **extra},
    }


def build(root):
    """把五個假工具和四份 .json 寫進 root，回一串 .json 的路徑。"""
    for name, body in TOOLS.items():
        path = Path(root, name)
        path.write_text(body, encoding="utf-8")
        path.chmod(0o755)

    def put(where, data):
        where = os.path.join(root, SPECS, where)
        spec.save(data, where)
        return where

    return [
        put("greet.json", one(
            root, "greet", limits={"who": {"max_bytes": 20}},
            argv={"who": {"position": 1},
                  "loud": {"position": 2, "flag": "--loud", "_type": "boolean"},
                  "note": {"position": 3, "flag": "--note"},
                  "tag": {"position": 4, "flag": "--tag", "repeat": True}})),
        put("count.json", one(root, "count", stdin="text")),
        put("seek.json", one(root, "seek", argv={"q": {"position": 1}},
                             required=["q"], ok_exit=[0, 1])),
        # 一個檔案裝兩個 tool，檔名跟裡面的名字都對不上 —— 名字由內容決定
        put("bundle.json", [one(root, "binary"),
                            one(root, "noisy", stdout={"clip": "tail"})]),
    ]
