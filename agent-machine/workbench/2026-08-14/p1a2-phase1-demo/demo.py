#!/usr/bin/env python3
"""One-command Phase 1 accept/recover demonstration; all mutable data is in /tmp."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
PROTOTYPE = HERE.parent / "p1a2-process-python" / "aos_p1a2.py"


def root_call() -> dict:
    # This is the prototype's exact Phase 1 Call shape.  Its two recipes are
    # frozen evidence here; Phase 1 accepts/recover them but does not execute them.
    return {
        "v": 1,
        "kind": "sequence_two",
        "children": [
            {"slot": "first", "recipe": {
                "kind": "process", "definition_path": "/opt/aos-fixture/process",
                "argv": ["/opt/aos-fixture/process", "success", "/work/aos"],
                "stdin": {"encoding": "base64", "data": "AP8="},
                "cwd": "/work/aos", "environment_policy": {"kind": "inherit"},
            }},
            {"slot": "second", "recipe": {"kind": "fake", "result": {
                "termination": {"kind": "exited", "code": 0},
                "stdout": {"encoding": "base64", "data": "U0VDT05ECg=="},
                "stderr": {"encoding": "base64", "data": ""},
            }}},
        ],
        "first_success_oracle": {
            "termination": {"kind": "exited", "code": 0},
            "stdout": {"encoding": "base64", "data": "UDFBMi1ERVRFUk1JTklTVElDLU9LCg=="},
            "stderr": {"encoding": "base64", "data": ""},
        },
    }


def run(*args: str) -> str:
    completed = subprocess.run(
        [sys.executable, str(PROTOTYPE), *args], text=True, capture_output=True,
        check=False, env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"},
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr.strip() or f"CLI exited {completed.returncode}")
    return completed.stdout.strip()


def main() -> int:
    if not PROTOTYPE.is_file():
        raise RuntimeError(f"prototype not found: {PROTOTYPE}")
    work = Path(tempfile.mkdtemp(prefix="aos-p1a2-phase1-demo-", dir="/tmp")).resolve()
    try:
        store = (work / "store").resolve()  # Deliberately an absolute Linux path.
        call_file = work / "root-call.json"
        call_file.write_text(json.dumps(root_call()), encoding="utf-8")
        accepted = run("--store", str(store), "accept", "--root-id", "root", "--call-file", str(call_file))
        recovered = run("--store", str(store), "recover")
        print("accept:  " + accepted)
        print("recover: " + recovered)
        if accepted != recovered:
            raise RuntimeError("accept and recover reports differ")
        print("PASS: accept -> recover output is byte-for-byte identical")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
