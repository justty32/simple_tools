"""invoke.py — spec + 模型給的 args → 真的去跑 → 一個字串。

**永遠回一個字串，錯誤也是字串，不丟例外** —— 跟 base_tools 同一套慣例，因為回傳值
會直接變成送回模型的 tool message。

**不經過 shell。**argv 是一個 list，`shell=False`，所以模型給的參數值裡有 `;`、
`$(...)`、空白都只是字元，不會被重新解析。這是這包相對於「叫模型用 run_shell」的
實質差別，不只是省 token。

三件輸出上的事，都是照 spec 宣告的來：`stderr.mode` 的 `merge` 是**真的重導向**
（子行程兩條管子共用一個，時序才對，不是事後字串相接）；`ok_exit` 之外的結束碼
才在最前面加一行 `exit N`（`grep` 沒找到是 exit 1，那不是失敗）；輸出含 NUL 就
當二進位，只回一句話而不是幾萬個替代字元。

危險程度跟 run_shell 同級（跑的是任意執行檔），所以用同一招：一個守門員 hook，
預設全放行，要人工放行自己接：

    exec_tools.set_approver(lambda name, argv: input(f"跑 {argv}？[y/N] ") == "y")
"""

import subprocess

from . import args as argmod

try:
    from base_tools.paths import MAX_OUTPUT
except ImportError:  # base_tools 不在也要能跑
    MAX_OUTPUT = 30000

_approver = None


def set_approver(fn):
    """設一個 fn(name, argv) -> bool 的守門員，回 False 就不執行。傳 None 取消。"""
    global _approver
    _approver = fn


def _decode(raw):
    """bytes → 給模型看的字串。含 NUL 就當二進位，不吐一堆替代字元灌爆 context。"""
    if not raw:
        return ""
    if b"\x00" in raw:
        return f"(binary output, {len(raw)} bytes, not shown)"
    return raw.decode("utf-8", errors="replace")


def _clip(text, where, limit=MAX_OUTPUT):
    """太長就截，並註明省略了多少。編譯器那種重點在尾巴的用 tail。"""
    if len(text) <= limit:
        return text
    cut = len(text) - limit
    if where == "tail":
        return f"… [truncated, {cut} earlier characters]\n{text[-limit:]}"
    return f"{text[:limit]}\n… [truncated, {cut} more characters]"


def _pipes(mode):
    if mode == "ignore":
        return {"stdout": subprocess.PIPE, "stderr": subprocess.DEVNULL}
    if mode == "only":
        return {"stdout": subprocess.DEVNULL, "stderr": subprocess.PIPE}
    # 真的把 stderr 導進 stdout 那條管子，這樣兩邊的先後順序才是實際發生的順序
    return {"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT}


def run(spec, arguments):
    """跑一次。arguments 就是模型給的那包 dict（llms 的 call["args"]）。"""
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
    text = _clip(_decode(raw).strip(), body.clip)
    if done.returncode in body.ok_exit:
        return text or f"(no output, exit {done.returncode})"
    return f"exit {done.returncode}\n{text}" if text else f"exit {done.returncode} (no output)"


def bind(spec):
    """包成一個 fn(**kwargs) -> str，好放進 dispatch 表給 llms 那套迴圈用。"""
    def call(**kwargs):
        return run(spec, kwargs)
    call.__name__ = spec.name
    return call
