"""Create the smallest Agent Machine Function."""

import os
from pathlib import Path


RUN = """#!/bin/sh
exec agent-machine run "$(dirname "$0")" "$@"
"""

FILES = {
    "llm.json": '{\n  "engine": "echo"\n}\n',
    "messages.json": "[]\n",
    "tools.json": "[]\n",
}


def create_function(path: Path) -> None:
    path.mkdir()
    run = path / "run"
    run.write_text(RUN, encoding="utf-8", newline="\n")
    os.chmod(run, 0o755)
    for name, content in FILES.items():
        (path / name).write_text(content, encoding="utf-8", newline="\n")
