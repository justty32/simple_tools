"""paths.py — 所有工具共用的工作根目錄、路徑檢查和輸出截斷。

路徑是模型講出來的字串，一律當成不可信：先接到 root 底下再 resolve，
resolve 完不在 root 裡就拒絕（symlink 想繞出去也會在這一步現形）。

    import base_tools
    base_tools.set_root("/tmp/workspace")   # 模型只能在這底下動手腳
"""

from pathlib import Path

MAX_OUTPUT = 30000  # 單次工具回傳的字元上限，別把模型的 context 灌爆
MAX_LINE = 2000     # 單行字元上限，minified js 這種一行幾百 KB 的不能整行吐出來

_root = Path.cwd().resolve()


def get_root() -> Path:
    """目前的工作根目錄，工具只能碰這底下的東西。"""
    return _root


def set_root(path) -> Path:
    """換一個工作根目錄，回傳解析後的絕對路徑。不存在也照設，之後才會報錯。"""
    global _root
    _root = Path(path).expanduser().resolve()
    return _root


def resolve(path):
    """把工具收到的路徑接到 root 底下解析，回傳 (Path, err)，能用時 err 是 None。

    相對路徑接在 root 後面，絕對路徑照用，但兩種都要落在 root 裡面才放行。
    """
    try:
        p = Path(str(path)).expanduser()
        full = (p if p.is_absolute() else _root / p).resolve()
    except Exception as e:  # 路徑字串本身壞掉（null byte、過長之類）
        return None, f"Error: bad path {path!r}: {e}"

    if not full.is_relative_to(_root):
        return None, f"Error: {path} is outside the workspace root {_root}"
    return full, None


def show(full: Path) -> str:
    """把絕對路徑縮成相對 root 的樣子，回給模型看的訊息用這個，比較短也比較安全。"""
    try:
        rel = full.relative_to(_root)
    except ValueError:
        return str(full)
    return str(rel) if str(rel) != "." else "."


def clip(text: str, limit: int = MAX_OUTPUT) -> str:
    """太長就截尾，並在結尾註明省略了多少 —— 讓模型知道它看到的不是全部。"""
    if len(text) <= limit:
        return text
    return text[:limit] + f"\n… [truncated, {len(text) - limit} more characters]"
