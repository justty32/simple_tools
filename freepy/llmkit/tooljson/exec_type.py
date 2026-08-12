"""exec_type.py — `_type: "exec"` 的解析器：跑一個 linux 檔案，argv + stdin/out/err。

`spec.py` 讀完 `_version` 和 `_type` 之後，把 `_extra` 其餘的鍵整包交給這裡。
**它是內建的，不是特別的** —— 檔案最後一行 `register("exec", ExecBody)` 走的是跟
第三方完全一樣的門（見 registry.py）。要加 python import、http，就照著寫一個平行的
檔案再登記一次，外殼一行都不用動。

只做解析和檢查，不跑東西 —— 真的去跑在 `invoke.py`，組 argv 在 `args.py`。
規範見 EXEC.md，這裡是把它寫成程式。
"""

import math
import os
import shutil

from .registry import register
from .spec import fingerprint, need, resolve

BINDING = ("position", "flag", "separate", "repeat")
CLIPS = ("head", "tail")
MODES = ("merge", "ignore", "only")


def _number(value):
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value))


class ExecBody:
    """一份 exec 型 spec 的執行配方。建構時把格式和路徑都算完。"""

    def __init__(self, spec):
        self.spec = spec
        self.extra = spec.extra
        self._exec(spec.dir)
        self._bindings(spec.props)
        self._stdio(spec.props)
        self._runtime(spec.props, spec.dir)

    def _exec(self, base_dir):
        raw = self.extra.get("exec")
        need(isinstance(raw, list) and raw and all(isinstance(x, str) and x for x in raw),
             "_extra.exec 要是非空的字串 list，第一項是程式本身")
        prog = raw[0]
        # 照 execvp 的老規矩看斜線：不含 / 的留給 $PATH，其餘以這份 .json 為中心
        self.exec = [prog if "/" not in prog else resolve(prog, base_dir), *raw[1:]]

    def _bindings(self, props):
        argv = self.extra.get("argv") or {}
        need(isinstance(argv, dict), "_extra.argv 要是 object，key 是參數名")
        for param, bind in argv.items():
            need(isinstance(bind, dict), f"_extra.argv[{param!r}] 要是 object")
            need(param in props, f"_extra.argv 綁了 {param!r}，但 parameters 裡沒這個")
            unknown = sorted(set(bind) - set(BINDING))
            need(not unknown, f"_extra.argv[{param!r}] 有不認得的鍵 {unknown}，可用的是 {list(BINDING)}")
            position = bind.get("position", 0)
            need(isinstance(position, int) and not isinstance(position, bool),
                 f"{param!r} 的 position 要是整數")
            need(isinstance(bind.get("flag", ""), str), f"{param!r} 的 flag 要是字串")
            need(isinstance(bind.get("separate", True), bool),
                 f"{param!r} 的 separate 要是 boolean")
            need(isinstance(bind.get("repeat", False), bool),
                 f"{param!r} 的 repeat 要是 boolean")
        #: 排序規則寫死才跨得了語言：position 小到大，同號的照參數名的碼位排
        self.order = sorted(argv.items(), key=lambda kv: (kv[1].get("position", 0), kv[0]))

    def _stdio(self, props):
        sin = self.extra.get("stdin")
        need(sin is None or (isinstance(sin, dict) and isinstance(sin.get("param"), str)),
             '_extra.stdin 要嘛是 null，要嘛是 {"param": "..."}')
        need(sin is None or set(sin) == {"param"},
             "_extra.stdin 只能有 param")
        self.stdin_param = sin["param"] if sin else None
        need(self.stdin_param is None or self.stdin_param in props,
             f"_extra.stdin 指了 {self.stdin_param!r}，但 parameters 裡沒這個")
        stdout = self.extra.get("stdout")
        need(stdout is None or isinstance(stdout, dict), "_extra.stdout 要是 object")
        need(stdout is None or set(stdout) <= {"clip"}, "_extra.stdout 只能有 clip")
        self.clip = (stdout or {}).get("clip", "head")
        need(self.clip in CLIPS, f"_extra.stdout.clip 是 {self.clip!r}，只認得 {list(CLIPS)}")
        stderr = self.extra.get("stderr")
        need(stderr is None or isinstance(stderr, dict), "_extra.stderr 要是 object")
        need(stderr is None or set(stderr) <= {"mode"}, "_extra.stderr 只能有 mode")
        self.stderr = (stderr or {}).get("mode", "merge")
        need(self.stderr in MODES, f"_extra.stderr.mode 是 {self.stderr!r}，只認得 {list(MODES)}")
        ok = self.extra.get("ok_exit", [0])
        need(isinstance(ok, list) and all(
            isinstance(x, int) and not isinstance(x, bool) for x in ok),
             "_extra.ok_exit 要是整數 list")
        self.ok_exit = ok or [0]

    def _runtime(self, props, base_dir):
        timeout = self.extra.get("timeout", 60)
        need(isinstance(timeout, (int, float)) and not isinstance(timeout, bool)
             and math.isfinite(timeout) and timeout > 0,
             "_extra.timeout 要是大於 0 的有限秒數")
        self.timeout = timeout
        cwd = self.extra.get("cwd")
        need(cwd is None or (isinstance(cwd, str) and cwd),
             "_extra.cwd 要嘛是 null，要嘛是非空路徑字串")
        self.cwd = resolve(cwd, base_dir) if cwd is not None else None

        limits = self.extra.get("limits")
        need(limits is None or isinstance(limits, dict), "_extra.limits 要是 object")
        self.limits = limits or {}
        unknown = sorted(set(self.limits) - set(props))
        need(not unknown, f"_extra.limits 有未知參數 {unknown}")
        for name, rule in self.limits.items():
            need(isinstance(rule, dict), f"_extra.limits[{name!r}] 要是 object")
            unknown = sorted(set(rule) - {"max_bytes", "min", "max"})
            need(not unknown, f"_extra.limits[{name!r}] 有不認得的鍵 {unknown}")
            cap = rule.get("max_bytes")
            need(cap is None or (isinstance(cap, int) and not isinstance(cap, bool)
                                 and cap >= 0),
                 f"_extra.limits[{name!r}].max_bytes 要是非負整數")
            low, high = rule.get("min"), rule.get("max")
            need(low is None or _number(low), f"_extra.limits[{name!r}].min 要是數字")
            need(high is None or _number(high), f"_extra.limits[{name!r}].max 要是數字")
            need(low is None or high is None or low <= high,
                 f"_extra.limits[{name!r}] 的 min 不可大於 max")

    @property
    def target(self):
        """真的會被執行的那個檔案的絕對路徑；`$PATH` 找不到就是 None。"""
        prog = self.exec[0]
        if "/" in prog:
            return prog if os.path.isfile(prog) else None
        return shutil.which(prog)

    def source(self):
        """現在這個檔案的指紋，給產 spec 的那步寫進 `_extra.source`。"""
        return fingerprint(self.target) if self.target else None

    def run(self, arguments) -> str:
        """body 協定要求的那個方法。真的去跑在 invoke.py，這裡只轉手。"""
        from .invoke import run_exec       # 延後 import：invoke 要用到 args，args 不用 body
        return run_exec(self.spec, arguments)


register("exec", ExecBody)
