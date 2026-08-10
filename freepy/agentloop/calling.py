"""calling.py — 一個 tool call → 一個字串。

迴圈本身在 [loop.py](loop.py)，這裡只管「叫下去、把結果收成一句模型讀得懂的話」。
分出來是因為它跟迴圈無關：給它一份 dispatch 和一筆 call 就能單獨叫、單獨驗。
"""

import inspect

#: 一個工具最多塞多少字元回去給模型。兩個現成的工具來源自己就截了，這裡是最後一道
#: —— `llms.to_tools()` 包的裸函式沒人替它截，一個 cat 大檔就能把 context 灌爆
MAX_OUTPUT = 30000


def perform(dispatch, call) -> str:
    """跑一個 tool call，**永遠回一個字串** —— 它會直接變成送回模型的 tool message。

    所以四種壞法都翻成模型讀得懂的一句話，而不是丟例外把整條迴圈打斷：參數不是
    合法 JSON、沒有這個工具、參數對不上、工具自己炸了。模型讀到 `Error: ...`
    會自己改一次再試，讀到 traceback 只會整條斷掉。
    """
    name = call.get("name") or "(unnamed)"
    args = call.get("args") or {}
    if "args_raw" in call:  # 小模型和被 max_tokens 切斷的輪都會這樣
        return (f"Error: the arguments for {name} are not valid JSON, so it was not run. "
                f"Got: {call['args_raw'][:200]}")

    fn = dispatch.get(name)
    if fn is None:
        return (f"Error: no such tool: {name}. "
                f"Available tools: {', '.join(sorted(dispatch)) or '(none)'}")

    bad = _mismatch(fn, args)
    if bad:
        return f"Error: {name} cannot take these arguments: {bad}"
    try:
        return _text(fn(**args))
    except Exception as e:
        return f"Error: {name} failed: {type(e).__name__}: {e}"


def _mismatch(fn, args):
    """參數對不對得上簽名，對不上就回那句話。

    **先問清楚再叫**：直接叫下去用 `except TypeError` 分辨的話，工具內部自己丟的
    TypeError 會被誤報成「參數對不上」，模型就照著那句假話去改一個本來沒問題的參數。
    """
    try:
        sig = inspect.signature(fn)
    except (TypeError, ValueError):
        return None  # 問不出簽名（C 實作的 builtin 之類），那就直接叫叫看
    try:
        sig.bind(**args)
    except TypeError as e:
        return str(e)
    return None


def _text(value) -> str:
    """工具回什麼都要變成字串，而且不能長到把 context 灌爆。"""
    if value is None:
        return "(no output)"
    out = value if isinstance(value, str) else str(value)
    if len(out) <= MAX_OUTPUT:
        return out
    return f"{out[:MAX_OUTPUT]}\n… [truncated, {len(out) - MAX_OUTPUT} more characters]"
