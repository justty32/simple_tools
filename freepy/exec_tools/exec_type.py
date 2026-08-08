"""exec_type.py — `_type: "exec"` 的解析器：跑一個 linux 檔案，argv + stdin/out/err。

`spec.py` 讀完 `_version` 和 `_type` 之後，把 `_extra` 其餘的鍵整包交給這裡。
以後的 python import、http 各自是一個平行的檔案，外殼不用動。

只做解析和檢查，不跑東西 —— 真的去跑在 `invoke.py`，組 argv 在 `args.py`。
規範見 EXEC.md，這裡是把它寫成程式。
"""

import os
import shutil

from .spec import fingerprint, need, resolve

BINDING = ("position", "flag", "separate", "repeat")
CLIPS = ("head", "tail")
MODES = ("merge", "ignore", "only")


class ExecBody:
    """一份 exec 型 spec 的執行配方。建構時把格式和路徑都算完。"""

    def __init__(self, spec):
        self.spec = spec
        self.extra = spec.extra
        self._exec(spec.dir)
        self._bindings(spec.props)
        self._stdio(spec.props)
        self.limits = self.extra.get("limits") or {}
        self.timeout = self.extra.get("timeout") or 60
        cwd = self.extra.get("cwd")  # None = 繼承呼叫端；模型傳的相對路徑全靠它解讀
        self.cwd = resolve(cwd, spec.dir) if cwd else None

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
            need(isinstance(bind.get("position", 0), int), f"{param!r} 的 position 要是整數")
            need(isinstance(bind.get("flag", ""), str), f"{param!r} 的 flag 要是字串")
        #: 排序規則寫死才跨得了語言：position 小到大，同號的照參數名的碼位排
        self.order = sorted(argv.items(), key=lambda kv: (kv[1].get("position", 0), kv[0]))

    def _stdio(self, props):
        sin = self.extra.get("stdin")
        need(sin is None or (isinstance(sin, dict) and isinstance(sin.get("param"), str)),
             '_extra.stdin 要嘛是 null，要嘛是 {"param": "..."}')
        self.stdin_param = sin["param"] if sin else None
        need(self.stdin_param is None or self.stdin_param in props,
             f"_extra.stdin 指了 {self.stdin_param!r}，但 parameters 裡沒這個")
        self.clip = (self.extra.get("stdout") or {}).get("clip", "head")
        need(self.clip in CLIPS, f"_extra.stdout.clip 是 {self.clip!r}，只認得 {list(CLIPS)}")
        self.stderr = (self.extra.get("stderr") or {}).get("mode", "merge")
        need(self.stderr in MODES, f"_extra.stderr.mode 是 {self.stderr!r}，只認得 {list(MODES)}")
        ok = self.extra.get("ok_exit", [0])
        need(isinstance(ok, list) and all(isinstance(x, int) for x in ok),
             "_extra.ok_exit 要是整數 list")
        self.ok_exit = ok or [0]

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
