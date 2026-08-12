#!/usr/bin/env python3
"""Shell entry helpers: prepare PYTHONPATH and replace the current process."""

import os
import sys
from pathlib import Path

FREEPY = Path(__file__).resolve().parent.parent
#: 定型的地基（llms / tooljson）在 llmkit/，還在長的東西在 freepy/ 這一層
LLMKIT = FREEPY / "llmkit"


def enter(program, *args, cwd=FREEPY):
    """Replace this process, optionally changing directory first.

    Coding-agent launchers keep the historical ``FREEPY`` default.  Callers
    such as the Python REPL can pass ``cwd=None`` to preserve the directory in
    which the operator started the shell.
    """
    env = dict(os.environ, PYTHONPATH=os.pathsep.join(
        filter(None, [str(FREEPY), str(LLMKIT), os.environ.get("PYTHONPATH")])))
    if cwd is not None:
        os.chdir(cwd)
    try:
        os.execvpe(program, [program, *args, *sys.argv[1:]], env)
    except FileNotFoundError:
        sys.exit(f"找不到 {program}，裝了嗎？")
