#!/usr/bin/env python3
"""__main__.py — 路由，把後面的參數整包轉給對應的入口檔。

    python -m shells repl
    python -m shells pi -c        # -c 之後的都是 pi 自己的參數
"""

import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NAMES = sorted(
    p.stem for p in HERE.glob("*.py")
    if not p.stem.startswith("_") and p.stem != "common"
)

name = sys.argv[1] if len(sys.argv) > 1 else ""
if name not in NAMES:
    sys.exit(f"用法: python -m shells [{' | '.join(NAMES)}] [參數...]")
os.execv(sys.executable, [sys.executable, str(HERE / f"{name}.py"), *sys.argv[2:]])
