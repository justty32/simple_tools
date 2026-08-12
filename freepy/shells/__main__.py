#!/usr/bin/env python3
"""__main__.py — 路由，把後面的參數整包轉給對應的入口檔。

    python -m shells repl
    python -m shells pi -c        # -c 之後的都是 pi 自己的參數
"""

import sys
from pathlib import Path

from .common import replace

HERE = Path(__file__).resolve().parent
NAMES = sorted(
    p.stem for p in HERE.glob("*.py")
    if not p.stem.startswith("_") and p.stem != "common"
)


def main(argv=None):
    args = sys.argv[1:] if argv is None else list(argv)
    name = args[0] if args else ""
    if name not in NAMES:
        sys.exit(f"用法: python -m shells [{' | '.join(NAMES)}] [參數...]")
    replace(sys.executable, [
        sys.executable, str(HERE / f"{name}.py"), *args[1:],
    ])


if __name__ == "__main__":
    main()
