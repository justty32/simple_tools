#!/usr/bin/env python3
"""Shell entry helpers: prepare PYTHONPATH and hand off to another process."""

import os
import shutil
import sys
from pathlib import Path

FREEPY = Path(__file__).resolve().parent.parent
#: 定型的地基（llms / tooljson）在 llmkit/，還在長的東西在 freepy/ 這一層
LLMKIT = FREEPY / "llmkit"


def replace(program, argv, env=None):
    """Replace this process, emulating that hand-off on Windows.

    CPython's Windows ``exec*`` implementation does not quote an argv sequence
    like ``subprocess`` does.  Arguments containing spaces can consequently be
    split, and the emulated exec can lose the child's exit status.  A direct
    child with inherited stdio preserves the console while avoiding both
    problems.  POSIX keeps real exec semantics.
    """
    if sys.platform == "win32":
        import subprocess
        path = (os.environ if env is None else env).get("PATH")
        executable = shutil.which(program, path=path) or program
        child_argv = [executable, *argv[1:]]
        raise SystemExit(subprocess.call(child_argv, env=env))
    if env is None:
        os.execv(program, argv)
    os.execvpe(program, argv, env)


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
        replace(program, [program, *args, *sys.argv[1:]], env)
    except FileNotFoundError:
        sys.exit(f"找不到 {program}，裝了嗎？")
