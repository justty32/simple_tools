"""tool.py — 自己宣告一個工具，然後存成 .json。

    import tooljson

    class Shout(tooljson.Tool):
        name = "shout"
        description = "把字變大聲"
        params = {"text": {"type": "string", "description": "要喊的字"},
                  "times": {"type": "integer", "description": "重複幾次"}}
        required = ["text"]

        def run(self, text, times=1) -> str:
            return " ".join([text.upper()] * times)

    tooljson.save(tooljson.from_tool(Shout, path="../mytools.py"), "specs/shout.json")

`path` 給了，.json 就自我完備 —— 讀取端不用把你的 `.py` 放進 `sys.path`，
相對路徑以 .json 自己的位置為中心。三種寫法（檔案／資料夾／不給）見 PYTHON.md。

**schema 是手寫的，不是從 signature 反射出來的。** 這是刻意的：`description` 和
`enum` 是寫給模型看的，函式簽名裡本來就沒有那些資訊 —— 反射猜得出型別，猜不出
「這個參數該怎麼用」。`params` 直接就是 JSON Schema 的 properties，不發明第二套
DSL：`enum`、`minimum`、`items` 都照 JSON Schema 寫，不用等這個 lib 支援。

存出來的 .json 用 `_type: "python"`（見 PYTHON.md），所以它讀得回來、跟 exec 型的
工具混在同一份檔案也合法。**如果你只是要在同一支程式裡用自己的函式，不需要這一套** ——
`llms.to_tools(fn)` 就夠了。這裡的價值在「能力變成一份設定檔」。
"""

import inspect
import os

from .spec import VERSION, fingerprint, need


class Tool:
    """繼承它，把四格填好，實作 `run()`。四格都有預設值，最少只要 `run()`。"""

    #: 模型看到的名字。留空就用 class 名 —— 那是給人看的名字，通常也夠好
    name = ""
    description = ""
    #: JSON Schema 的 properties，原封不動
    params = {}
    required = ()

    def run(self, **kwargs) -> str:
        """做事，回一個字串。**錯誤也回字串**，因為它會直接變成 tool message。"""
        raise NotImplementedError(f"{type(self).__name__} 還沒實作 run()")

    def __call__(self, **kwargs):
        """讓實體本身就是 callable，執行端才不用分「函式」和「Tool」兩條路。"""
        return self.run(**kwargs)

    @classmethod
    def tool_name(cls):
        return cls.name or cls.__name__

    @classmethod
    def schema(cls) -> dict:
        """乾淨的 OpenAI tool dict。宣告寫錯就丟 SpecError —— 那是設定錯，越早炸越好。"""
        name = cls.tool_name()
        need(isinstance(name, str) and name, f"{cls.__name__}: name 要是非空字串")
        need(isinstance(cls.params, dict), f"{name}: params 要是 dict（JSON Schema 的 properties）")
        required = list(cls.required or ())
        unknown = sorted(set(required) - set(cls.params))
        need(not unknown, f"{name}: required 列了 params 裡沒有的 {unknown}")
        return {
            "type": "function",
            "function": {
                "name": name,
                "description": (cls.description or "").strip(),
                "parameters": {
                    "type": "object",
                    "properties": dict(cls.params),
                    "required": required,
                },
            },
        }


def from_tool(cls, module=None, attr=None, path=None) -> dict:
    """一個 Tool 子類別 → 一份完整的 spec dict，可以直接丟給 `save()`。

    `module` / `attr` 不給就從 class 自己問出來，**包括 `python weather.py` 這樣直接
    執行時**：那種情況 `cls.__module__` 是 `"__main__"`，是「正在被執行的那支程式」的
    意思，別的行程 import 不到，所以改從檔名反推（`weather.py` → `weather`）。
    存出來的 .json 是要給別的行程讀的，不是給現在這支。

    `path` 是選填的，指出模組在哪（檔案或資料夾，相對這份 .json）。不給就純靠
    `sys.path` 找 `module` —— 那對已經裝好的套件才成立，自己放在某個資料夾裡的
    一個 .py 檔要給 `path`，.json 才自我完備。
    """
    data = cls.schema()
    extra = {
        "_version": VERSION,
        "_type": "python",
        "module": module or _module_name(cls),
        "attr": attr or cls.__name__,
    }
    if path:
        extra["path"] = path
    # 記下產這份 spec 當下那個 .py 的指紋，`Spec.stale` 才有東西可比 —— 函式改了
    # 簽名、schema 沒跟著改，就會被問出來。問不到檔案（REPL 裡定義的）就不記，
    # 因為 `stale` 的 None 本來就是「不知道」，寧可不知道也不要記一個假的
    source = fingerprint(_source_file(cls))
    if source:
        extra["source"] = source
    data["_extra"] = extra
    return data


def _module_name(cls):
    """class 自己說的模組名。`"__main__"` 要從檔案位置反推，那個名字別人 import 不到。

    反推不出來（真的就定義在一個叫 `__main__.py` 的檔裡、或 REPL 裡）就丟，
    **不要猜一個** —— 猜錯的話 .json 現在存得出來，等別的行程去讀才炸。
    """
    name = getattr(cls, "__module__", "") or ""
    if name != "__main__":
        return name
    dotted = _dotted_name(_source_file(cls))
    need(dotted,
         f"{cls.__name__} 定義在 __main__ 裡，反推不出別的行程 import 得到的模組名。"
         f"請自己給 from_tool(..., module='你的檔名不含 .py')")
    return dotted


def _dotted_name(path):
    """檔案位置 → 別的行程 import 得到的名字。反推不出來就回空字串。

    只看檔名不夠：`python -m base_tools.specs` 的 `specs.py` 住在一個 package 裡，
    反推成 `"specs"` 是**錯的**（要 `"base_tools.specs"`），而且錯得很安靜 ——
    .json 存得出來，等別的行程去讀才 import 不到。所以一路往上收有 `__init__.py`
    的資料夾。
    """
    if not path:  # 問不到檔案（REPL 裡定義的）。abspath("") 是 cwd，會反推出一個假名字
        return ""
    directory, filename = os.path.split(os.path.abspath(path))
    stem = os.path.splitext(filename)[0]
    if not stem or stem == "__main__":  # 真的就叫 __main__.py，檔名救不了
        return ""
    parts = [stem]
    while os.path.isfile(os.path.join(directory, "__init__.py")):
        directory, package = os.path.split(directory)
        if not package:
            break
        parts.append(package)
    return ".".join(reversed(parts))


def _source_file(cls):
    try:
        return inspect.getfile(cls)
    except (TypeError, OSError):
        return ""
