#!/usr/bin/env python3
"""common.py — 三個入口共用的那幾行：固定工作目錄、PYTHONPATH、然後換掉自己。"""

import os
import sys
from pathlib import Path

FREEPY = Path(__file__).resolve().parent.parent
#: 定型的地基（llms / tooljson）在 llmkit/，還在長的東西在 freepy/ 這一層
LLMKIT = FREEPY / "llmkit"


def enter(program, *args):
    """換掉自己（execvp，不開子 process），Ctrl-C、TTY、退出碼全部直通。"""
    env = dict(os.environ, PYTHONPATH=os.pathsep.join(
        filter(None, [str(FREEPY), str(LLMKIT), os.environ.get("PYTHONPATH")])))
    os.chdir(FREEPY)
    try:
        os.execvpe(program, [program, *args, *sys.argv[1:]], env)
    except FileNotFoundError:
        sys.exit(f"找不到 {program}，裝了嗎？")
