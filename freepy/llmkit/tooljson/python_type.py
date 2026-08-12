"""python_type.py — `_type: "python"` 的解析器：叫一個 python 物件。

`spec.py` 讀完兩個保留鍵之後，把 `_extra` 其餘的鍵整包交給這裡。**它跟 `exec` 一樣
是內建的、一樣沒有特權** —— 檔案最後一行 `register("python", PythonBody)` 走的是跟
第三方完全一樣的門。規範見 PYTHON.md。

比 exec 型簡單很多：模型給的那包 dict 直接 `**` 進去，不需要 argv / stdin / position
那一整套 —— 那些是「怎麼把 dict 攤成命令列」的問題，python 這邊根本沒有那道關卡。

**import 是建構時就做的。** 也就是 `load()` 一份指到不存在的模組的 .json 會當場丟
SpecError，而不是等到 `run()` 才回字串。跟 exec 型「找不到執行檔時 run() 回字串給
模型看」的行為不一致，這是知道的：模組不見了是設定錯（給人看），執行檔在模型叫的
當下才不見比較像環境問題（給模型看）。安全那面靠外圍沙盒，不在這一層處理。
"""

import importlib
import importlib.machinery
import importlib.util
import json
import os
import sys

from .registry import register
from .spec import SpecError, need, resolve
from .text import clip


_MISSING = object()


def _path_id(path):
    """A comparison key for source paths (including Windows' case folding)."""
    return os.path.normcase(os.path.realpath(os.path.abspath(path)))


def _module_source(module):
    """Return the concrete file, or namespace-package search locations."""
    file = getattr(module, "__file__", None)
    if file:
        return "file", _path_id(file)
    locations = getattr(module, "__path__", None)
    if locations is not None:
        return "namespace", tuple(_path_id(one) for one in locations)
    return None


def _spec_source(found):
    origin = getattr(found, "origin", None)
    if origin and origin not in {"built-in", "frozen"}:
        return "file", _path_id(origin)
    locations = getattr(found, "submodule_search_locations", None)
    if locations is not None:
        return "namespace", tuple(_path_id(one) for one in locations)
    return None


def as_text(value) -> str:
    """函式回什麼都要變成一個字串 —— body 協定要求，而且它會直接變成 tool message。

    規則寫死：字串原樣、`None` 是「跑完了但沒東西講」、其餘 JSON 化（`default=str`
    讓 Path / datetime 這種也過得去），真的序列化不了才 `str()`。
    """
    if isinstance(value, str):
        return value
    if value is None:
        return "(no output)"
    try:
        return json.dumps(value, ensure_ascii=False, default=str)
    except Exception:
        return str(value)


class PythonBody:
    """一份 python 型 spec 的執行配方。建構時就把模組載進來、物件找出來。"""

    def __init__(self, spec):
        self.spec = spec
        extra = spec.extra
        self.module = extra.get("module")
        need(isinstance(self.module, str) and self.module,
             "_extra.module 要是非空字串（import 路徑；有 path 時是那個模組的名字）")
        self.attr = extra.get("attr")
        need(isinstance(self.attr, str) and self.attr,
             "_extra.attr 要是非空字串，指出模組裡的哪個物件")
        # 相對路徑以這份 .json 為中心，跟 FORMAT.md 那條通則一樣
        self.path = resolve(extra.get("path"), spec.dir) if extra.get("path") else None
        self.file = None
        self.obj = self._import()

    def _import(self):
        module = self._module()
        self.file = getattr(module, "__file__", None)
        obj = getattr(module, self.attr, None)
        need(obj is not None, f"{self.module} 裡沒有 {self.attr!r}")
        need(callable(obj), f"{self.module}.{self.attr} 不是 callable，叫不動")
        return obj

    def _module(self):
        """三種找法：給檔案、給資料夾、什麼都不給純靠 sys.path。"""
        if self.path and os.path.isfile(self.path):
            return self._from_file(self.path)
        if self.path:
            need(os.path.isdir(self.path),
                 f"_extra.path 指的 {self.path!r} 不是檔案也不是資料夾")
            expected = self._find_in_directory(self.path)
            self._check_cached(expected)
            # `path` 是明確來源，不只是另一個候選路徑；確實把它移到最前面。
            wanted = _path_id(self.path)
            sys.path[:] = [one for one in sys.path
                           if not isinstance(one, str) or _path_id(one) != wanted]
            sys.path.insert(0, self.path)
        try:
            module = importlib.import_module(self.module)
            if self.path:
                need(_module_source(module) == _spec_source(expected[-1][1]),
                     f"import {self.module!r} 沒有使用 _extra.path 指定的來源 {self.path!r}")
            return module
        except SpecError:
            raise
        except Exception as e:
            need(False, f"import {self.module!r} 失敗：{type(e).__name__}: {e}")

    def _find_in_directory(self, root):
        """Resolve every dotted-name component under one explicit filesystem root."""
        # Specs and their modules are often generated in the same process.  Some
        # FileFinders otherwise keep a just-created package out of view briefly.
        importlib.invalidate_caches()
        found = []
        search = [root]
        prefix = ""
        for part in self.module.split("."):
            prefix = f"{prefix}.{part}" if prefix else part
            one = importlib.machinery.PathFinder.find_spec(prefix, search)
            need(one is not None,
                 f"_extra.path {root!r} 底下找不到模組 {prefix!r}")
            found.append((prefix, one))
            search = one.submodule_search_locations
            if prefix != self.module:
                need(search is not None,
                     f"{prefix!r} 不是 package，不能載入 {self.module!r}")
        need(_spec_source(found[-1][1]) is not None,
             f"_extra.path {root!r} 底下的 {self.module!r} 沒有可辨識的來源")
        return found

    def _check_cached(self, expected):
        """Refuse a cached module/package that would override an explicit source."""
        for name, found in expected:
            cached = sys.modules.get(name)
            if cached is not None and _module_source(cached) != _spec_source(found):
                need(False, f"模組 {name!r} 已從別處載入，與 _extra.path {self.path!r} 衝突")

    def _from_file(self, path):
        cached = sys.modules.get(self.module)
        wanted = ("file", _path_id(path))
        if cached is not None:
            need(_module_source(cached) == wanted,
                 f"模組 {self.module!r} 已從別處載入，與 _extra.path {path!r} 衝突")
            return cached
        previous = sys.modules.get(self.module, _MISSING)
        try:
            # 這裡的 spec 是 importlib 的 ModuleSpec，跟 tooljson 的 Spec 沒關係；
            # 真正會跑 code 的是它的 .loader，不是它自己
            found = importlib.util.spec_from_file_location(self.module, path)
            need(found is not None and found.loader is not None, f"{path} 載不成模組")
            module = importlib.util.module_from_spec(found)
            # 先進 sys.modules 再 exec：模組裡的 dataclass / pickle 要找得到自己
            sys.modules[self.module] = module
            found.loader.exec_module(module)
        except SpecError:
            if previous is _MISSING:
                sys.modules.pop(self.module, None)
            else:
                sys.modules[self.module] = previous
            raise
        except Exception as e:
            if previous is _MISSING:
                sys.modules.pop(self.module, None)
            else:
                sys.modules[self.module] = previous
            need(False, f"載入 {path} 失敗：{type(e).__name__}: {e}")
        return module

    @property
    def target(self):
        """模組的檔案，給 `Spec.stale` 算指紋。函式的簽名改了 spec 就過期了。"""
        return self.file

    def run(self, arguments) -> str:
        """body 協定要求的那個方法。**永遠回字串，錯誤也是字串，不丟例外。**"""
        target = self.obj
        try:
            if isinstance(target, type):
                # Tool 子類別：每次叫都給一個乾淨的實體，兩次呼叫之間不留狀態
                target = target()
            out = target(**(arguments or {}))
        except TypeError as e:
            # 參數對不上是模型的錯，講清楚讓它自己改一次
            return f"Error: {self.spec.name} 收不下這組參數：{e}"
        except Exception as e:
            return f"Error: {self.spec.name} failed: {type(e).__name__}: {e}"
        return clip(as_text(out))


register("python", PythonBody)
