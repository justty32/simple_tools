"""invoke.py — `_type: "exec"` 的執行端：spec + 模型給的 args → 真的去跑 → 一個字串。

**這個檔是 exec 專屬的**，不是通用的執行入口。通用的入口是 `Spec.run()`，它只轉手給
`_type` 自己的 body；`ExecBody.run()` 才會走到這裡。別的 `_type` 有自己的執行端。

**永遠回一個字串，錯誤也是字串，不丟例外** —— 因為回傳值會直接變成送回模型的
tool message。模型讀到「找不到執行檔」是能自己換一步走的，讀到例外只會整條斷掉。

**不經過 shell。**argv 是一個 list，`shell=False`，所以模型給的參數值裡有 `;`、
`$(...)`、空白都只是字元，不會被重新解析。這是相對於「叫模型自己寫 shell 指令」的
實質差別，不只是省 token。

三件輸出上的事，都是照 spec 宣告的來：`stderr.mode` 的 `merge` 是**真的重導向**
（子行程兩條管子共用一個，時序才對，不是事後字串相接）；`ok_exit` 之外的結束碼
才在最前面加一行 `exit N`（`grep` 沒找到是 exit 1，那不是失敗）；輸出含 NUL 就
當二進位，只回一句話而不是幾萬個替代字元。

跑的是任意執行檔，危險程度跟給模型一個 shell 同級，所以留一個守門員 hook，
預設全放行，要人工放行自己接：

    tooljson.set_approver(lambda name, argv: input(f"跑 {argv}？[y/N] ") == "y")

守門員**也是 exec 專屬的**（第二個參數就是要跑的 argv）。別的 `_type` 想要放行機制
就自己留一個，形狀由它自己決定 —— 硬湊一個通用簽章只會讓兩邊都難用。
"""

import subprocess

from . import args as argmod
from .text import clip, decode

_approver = None


def set_approver(fn):
    """設一個 fn(name, argv) -> bool 的守門員，回 False 就不執行。傳 None 取消。"""
    global _approver
    _approver = fn


def _pipes(mode):
    if mode == "ignore":
        return {"stdout": subprocess.PIPE, "stderr": subprocess.DEVNULL}
    if mode == "only":
        return {"stdout": subprocess.DEVNULL, "stderr": subprocess.PIPE}
    # 真的把 stderr 導進 stdout 那條管子，這樣兩邊的先後順序才是實際發生的順序
    return {"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT}


def run_exec(spec, arguments):
    """跑一次 exec 型的 spec。arguments 就是模型給的那包 dict（llms 的 call["args"]）。"""
    body = spec.body
    argv, stdin, err = argmod.build(spec, arguments)
    if err:
        return err
    if body.target is None:
        # 分開講：找不到檔案跟跑起來失敗，模型該做的事不一樣
        return f"Error: cannot find the executable {body.exec[0]!r} for tool {spec.name!r}"

    if _approver is not None:
        try:
            allowed = _approver(spec.name, argv)
        except Exception as e:
            return f"Error: approval check failed: {e}"
        if not allowed:
            return "Error: the user declined to run this tool"

    try:
        done = subprocess.run(
            # 沒有 stdin 也要餵一個空的：不然子行程會去讀我們的 stdin，一路卡到 timeout
            argv, input=(stdin or "").encode("utf-8"), cwd=body.cwd,
            timeout=body.timeout, **_pipes(body.stderr),
        )
    except subprocess.TimeoutExpired:
        # POSIX 上 subprocess.run 逾時後不會再 communicate 一次，撈不到半截輸出
        return f"Error: {spec.name} timed out after {body.timeout}s"
    except OSError as e:
        return f"Error: cannot run {spec.name}: {e}"
    except Exception as e:
        return f"Error: {spec.name} failed to start: {e}"

    raw = done.stderr if body.stderr == "only" else done.stdout
    text = clip(decode(raw).strip(), body.clip)
    if done.returncode in body.ok_exit:
        return text or f"(no output, exit {done.returncode})"
    return f"exit {done.returncode}\n{text}" if text else f"exit {done.returncode} (no output)"


def bind(spec):
    """包成一個 fn(**kwargs) -> str，好放進 dispatch 表給 llms 那套迴圈用。

    走的是 `Spec.run()` 而不是這個檔裡的 `run_exec()`，所以第三方登記的 `_type`
    一樣 bind 得起來。
    """
    def call(**kwargs):
        return spec.run(kwargs)
    call.__name__ = spec.name
    call.__doc__ = (spec.function.get("description") or "").strip() or None
    return call
