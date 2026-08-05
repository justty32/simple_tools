#!/usr/bin/env python3
"""repl.py — 開一個 python REPL，llms 和 base_tools 已經 import 好。"""

import sys
from pathlib import Path

from common import FREEPY, enter

# 用 freepy 自己的 venv，不然從隨便一個 shell 叫進來會找不到 openai
NAME = "Scripts/python.exe" if sys.platform == "win32" else "bin/python"
VENV = FREEPY / ".venv" / NAME

BOOT = ("import llms, base_tools; from llms import LLM, Params; "
        "print('已就緒: llms(LLM, Params), base_tools')")

enter(str(VENV) if VENV.exists() else sys.executable, "-i", "-c", BOOT)
