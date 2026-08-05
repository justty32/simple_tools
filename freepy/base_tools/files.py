"""files.py — 讀檔和寫檔。

read_file 吐出來的每一行都帶行號，格式跟 `cat -n` 一樣：模型引用位置時有東西可指，
接著要 edit_file 也比較不會抄錯。錯誤一律是回傳的字串，不丟例外 —— 理由見 __init__.py。
"""

from .paths import MAX_LINE, clip, resolve, show


def _numbered(lines, start):
    """把 (行號, 內容) 排成 `     1\tfoo`，過長的行就地截斷。"""
    out = []
    for i, line in enumerate(lines, start):
        if len(line) > MAX_LINE:
            line = line[:MAX_LINE] + f"… [line truncated, {len(line) - MAX_LINE} more]"
        out.append(f"{i:6d}\t{line}")
    return "\n".join(out)


def read_file(path: str, offset: int = 1, limit: int = 2000) -> str:
    """讀一個文字檔，回傳的每一行前面都附上行號。

    Args:
        path: 檔案路徑，相對路徑會接在工作根目錄底下
        offset: 從第幾行開始讀，從 1 算起
        limit: 最多讀幾行，檔案很大時分次讀
    """
    full, err = resolve(path)
    if err:
        return err
    if full.is_dir():
        return f"Error: {show(full)} is a directory, not a file"

    try:
        data = full.read_bytes()
    except FileNotFoundError:
        return f"Error: file not found: {show(full)}"
    except Exception as e:
        return f"Error: cannot read {show(full)}: {e}"

    if b"\x00" in data[:8192]:
        return f"Error: {show(full)} looks like a binary file ({len(data)} bytes)"
    text = data.decode("utf-8", errors="replace")
    if not text:
        return f"({show(full)} is empty)"

    try:
        start = max(1, int(offset))
        count = max(1, int(limit))
    except (TypeError, ValueError):
        return f"Error: offset and limit must be integers, got {offset!r} and {limit!r}"

    lines = text.splitlines()
    chunk = lines[start - 1:start - 1 + count]
    if not chunk:
        return f"Error: {show(full)} has only {len(lines)} lines, offset {start} is past the end"

    body = _numbered(chunk, start)
    rest = len(lines) - (start - 1 + len(chunk))
    if rest > 0:
        body += f"\n… [{rest} more lines, read again with offset={start + len(chunk)}]"
    return clip(body)


def write_file(path: str, content: str) -> str:
    """把文字寫進檔案。檔案已經存在就整個覆寫，父資料夾不存在會自動建立。

    Args:
        path: 檔案路徑，相對路徑會接在工作根目錄底下
        content: 要寫進去的完整內容
    """
    full, err = resolve(path)
    if err:
        return err
    if full.is_dir():
        return f"Error: {show(full)} is a directory, not a file"

    existed = full.exists()
    text = content if isinstance(content, str) else str(content)
    try:
        full.parent.mkdir(parents=True, exist_ok=True)
        full.write_text(text, encoding="utf-8")
    except Exception as e:
        return f"Error: cannot write {show(full)}: {e}"

    verb = "Overwrote" if existed else "Created"
    n = len(text.splitlines())
    return f"{verb} {show(full)} ({n} lines, {len(text.encode('utf-8'))} bytes)"
