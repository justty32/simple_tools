#!/usr/bin/env python3
"""repl.py — 開一個預載 FreePy 本地互動 API 的 Python REPL。"""

import os
import sys
from pathlib import Path

from common import FREEPY, enter

# 用 freepy 自己的 venv，不然從隨便一個 shell 叫進來會找不到 openai
NAME = "Scripts/python.exe" if sys.platform == "win32" else "bin/python"
VENV = FREEPY / ".venv" / NAME

os.environ["PYTHONSTARTUP"] = str(Path(__file__).with_name("_startup.py"))

enter(str(VENV) if VENV.exists() else sys.executable, "-i", cwd=None)
