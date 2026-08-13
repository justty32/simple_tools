"""spec.py — spec 的外殼：讀寫、保留鍵、把其餘的鍵轉交給對應的解析器。

磁碟上長這樣：

    {
      "type": "function",
      "function": {"name": ..., "description": ..., "parameters": {...}},
      "_extra": {"_version": "0.1.0", "_type": "exec", "exec": ["../resize"], ...}
    }

前半是原封不動的 OpenAI tool，`spec.schema` 就是把 `_extra` 剝掉 —— **送進
`Bot(tools=...)` 之前一定要剝**，多一個未知的鍵，OpenAI / LiteLLM / LM Studio
三邊各有各的嫌法，不值得賭。

`_extra` 裡只有 `_version` 和 `_type` 兩個保留鍵，**這個檔只讀這兩個**，其餘一律
交給 `_type` 指定的解析器，結果掛在 `spec.body`；`spec.run()` 也只是轉手給它。
**這個檔不知道有哪些 `_type`** —— 它去問 registry.py，所以第三方登記的跟內建的
`exec` 走同一條路，這裡一行都不用改。

格式規範見 FORMAT.md（外殼）和 EXEC.md（`_type: "exec"` 那套）。
"""

import hashlib
import json
import os

from .registry import parser, types

VERSION = "0.1.0"


class SpecError(ValueError):
    """spec 檔本身壞掉。這是設定錯，不是模型的錯，所以用丟的，不回字串。"""


def need(cond, msg):
    """檢查不過就丟。分出來純粹是為了讓每條規則佔一行，看得出總共檢查了什麼。"""
    if not cond:
        raise SpecError(msg)


def resolve(value, base_dir):
    """`_extra` 裡的相對路徑 → 絕對路徑，中心是 .json 所在的資料夾。"""
    need(isinstance(value, str) and value, f"路徑要是非空字串，拿到 {value!r}")
    return os.path.abspath(os.path.join(base_dir, value))


def fingerprint(path):
    """檔案指紋，用來判斷 spec 有沒有過期。讀不到就回 None（不知道，不是沒變）。"""
    try:
        stat = os.stat(path)
        with open(path, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
    except OSError:
        return None
    return {"size": stat.st_size, "mtime": int(stat.st_mtime), "sha256": digest}


def _parameters(function):
    """Validate the object-shaped subset every execution body depends on."""
    if "description" in function:
        need(isinstance(function["description"], str),
             "function.description 要是字串")
    if "parameters" not in function:
        return {}, []
    parameters = function["parameters"]
    need(isinstance(parameters, dict), "function.parameters 要是 object")
    need(parameters.get("type", "object") == "object",
         "function.parameters.type 只能是 'object'")
    props = parameters.get("properties", {})
    need(isinstance(props, dict), "function.parameters.properties 要是 object")
    need(all(isinstance(name, str) and name and isinstance(rule, dict)
             for name, rule in props.items()),
         "function.parameters.properties 要把非空參數名映到 schema object")
    required = parameters.get("required", [])
    need(isinstance(required, list)
         and all(isinstance(name, str) and name for name in required),
         "function.parameters.required 要是非空字串 list")
    need(len(required) == len(set(required)),
         "function.parameters.required 不可有重複名稱")
    unknown = sorted(set(required) - set(props))
    need(not unknown, f"function.parameters.required 有未知參數 {unknown}")
    return props, required


class Spec:
    """一份 spec。保留鍵在這裡，`_type` 專屬的東西在 `spec.body`。"""

    def __init__(self, data, path=None):
        self.path = os.path.abspath(path) if path else None
        self.dir = os.path.dirname(self.path) if self.path else os.getcwd()
        self.data = data
        need(isinstance(data, dict) and data.get("type") == "function",
             '最外層要是 {"type": "function", ...}')
        fn = data.get("function")
        need(isinstance(fn, dict) and isinstance(fn.get("name"), str) and fn["name"],
             "function.name 缺了或不是非空字串")
        extra = data.get("_extra")
        need(isinstance(extra, dict), "_extra 缺了；沒有它這份 JSON 只是 schema，跑不起來")
        # 0.x 期間 minor 就等於 major，所以認不得的版本一律拒絕，不猜
        need(extra.get("_version") == VERSION,
             f"_extra._version 是 {extra.get('_version')!r}，這支只認得 {VERSION!r}")
        make = parser(extra.get("_type"))
        need(make is not None,
             f"_extra._type 是 {extra.get('_type')!r}，目前登記的只有 {types()}。"
             f"自己的執行方式用 tooljson.register() 加進來")
        self.function, self.extra, self.name = fn, extra, fn["name"]
        self.version, self.kind = extra["_version"], extra["_type"]
        self.props, self.required = _parameters(fn)
        self.body = make(self)

    @property
    def schema(self) -> dict:
        """剝掉 `_extra` 的乾淨 OpenAI tool，可以進 `Bot(tools=...)` bundle。"""
        return {"type": "function", "function": self.function}

    def run(self, arguments) -> str:
        """跑一次，回一個字串。實際做事的是 `_type` 那邊的 body，這裡只轉手。"""
        return self.body.run(arguments)

    @property
    def stale(self):
        """產 spec 當下那個來源變了沒。沒記 source 或來源不見了都回 None（不知道）。"""
        source = self.extra.get("source")
        old = source.get("sha256") if isinstance(source, dict) else None
        target = self.body.target
        now = fingerprint(target) if old and target else None
        return None if now is None else now["sha256"] != old


def load_all(path) -> list:
    """讀一個檔案裡的所有 spec。JSON 壞掉或格式不對都丟 SpecError。

    最外層可以是一個 object（一個 tool），也可以是一個 array（一個檔案裝好幾個
    tool，相關的工具擺在一起比較好維護）。同一個檔案裡撞名是錯，不是先到先贏 ——
    那明顯是打錯字，安靜挑一個只會讓人找不到另一個。
    """
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        raise SpecError(f"{path} 讀不起來：{e}") from e
    items = data if isinstance(data, list) else [data]
    need(items, f"{path} 是空的 array，一個 tool 都沒有")
    out = []
    for i, one in enumerate(items):
        try:
            out.append(Spec(one, path))
        except SpecError as e:
            raise SpecError(f"{path} 第 {i + 1} 個：{e}") from e
    names = [one.name for one in out]
    dupes = sorted({n for n in names if names.count(n) > 1})
    need(not dupes, f"{path} 裡有重複的 function.name {dupes}")
    return out


def load(path) -> Spec:
    """只要一個 spec 的方便寫法；檔案裡不只一個就丟 SpecError。"""
    found = load_all(path)
    need(len(found) == 1, f"{path} 裡有 {len(found)} 個 tool，用 load_all() 讀")
    return found[0]


def run(spec, arguments) -> str:
    """跑一次，回一個字串。跟 `spec.run(arguments)` 完全一樣，只是寫起來順一點。"""
    return spec.run(arguments)


def save(data, path):
    """寫出一份（dict）或一串（list）spec。中文不轉義，因為描述本來就是中文。"""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")
