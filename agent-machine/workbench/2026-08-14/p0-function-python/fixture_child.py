"""Deliberately small child process used only by the prototype tests."""

from __future__ import annotations

import json
import os
from pathlib import Path
import signal
import sys
import time


def main() -> int:
    command = sys.argv[1]
    if command == "argv":
        print(json.dumps(sys.argv[2:], ensure_ascii=False))
    elif command == "echo":
        sys.stdout.buffer.write(sys.stdin.buffer.read())
    elif command == "split":
        amount = int(sys.argv[2])
        sys.stdout.buffer.write(b"O" * amount)
        sys.stderr.buffer.write(b"E" * amount)
        return 7
    elif command == "sigterm":
        os.kill(os.getpid(), signal.SIGTERM)
    elif command == "side-effect":
        Path(sys.argv[2]).write_text("executed", encoding="utf-8")
        time.sleep(float(sys.argv[3]))
    elif command == "cwd":
        sys.stdout.buffer.write(os.getcwd().encode() + b"\n")
    else:
        raise ValueError(command)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
