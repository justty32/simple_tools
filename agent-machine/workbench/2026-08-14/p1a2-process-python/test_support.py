"""Shared Linux-/tmp-only helpers for P1a-2 tests."""
from __future__ import annotations

import base64
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
CLI = HERE / "aos_p1a2.py"
ROOT_ID = "root"


def bytes_value(data: bytes) -> dict[str, str]:
    return {"encoding": "base64", "data": base64.b64encode(data).decode("ascii")}


def exited(code: int, stdout: bytes = b"", stderr: bytes = b"") -> dict[str, Any]:
    return {"termination": {"kind": "exited", "code": code},
            "stdout": bytes_value(stdout), "stderr": bytes_value(stderr)}


def root_call(definition: str = "/opt/aos-fixture/process",
              cwd: str = "/work/aos", stdin: bytes = b"\x00\xff") -> dict[str, Any]:
    return {
        "v": 1,
        "kind": "sequence_two",
        "children": [
            {"slot": "first", "recipe": {
                "kind": "process", "definition_path": definition,
                "argv": [definition, "success", cwd], "stdin": bytes_value(stdin),
                "cwd": cwd, "environment_policy": {"kind": "inherit"},
            }},
            {"slot": "second", "recipe": {"kind": "fake", "result": exited(0, b"SECOND\n")}},
        ],
        "first_success_oracle": exited(0, b"P1A2-DETERMINISTIC-OK\n"),
    }


def root_fake_call(*, oracle_stdout: bytes = b"ok\n") -> dict[str, Any]:
    ok = exited(0, b"ok\n")
    return {
        "v": 1, "kind": "sequence_two_fake",
        "children": [
            {"slot": "first", "recipe": {"kind": "fake", "result": ok}},
            {"slot": "second", "recipe": {"kind": "fake", "result": ok}},
        ],
        "first_success_oracle": exited(0, oracle_stdout),
    }


def write_call(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")


def run_cli(store: Path, command: str, *, call_file: Path | None = None,
            root_id: str = ROOT_ID, failpoint: str | None = None,
            timeout: float = 10.0) -> subprocess.CompletedProcess[str]:
    cmd = [sys.executable, str(CLI), "--store", str(store)]
    if failpoint is not None:
        cmd += ["--failpoint", failpoint]
    cmd.append(command)
    if command == "accept":
        assert call_file is not None
        cmd += ["--root-id", root_id, "--call-file", str(call_file)]
    env = dict(os.environ)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    result = subprocess.run(cmd, text=True, capture_output=True, check=False,
                            timeout=timeout, env=env)
    if result.returncode == 97 and failpoint is not None:
        sidecar = store.with_name(store.name + ".failpoint-used")
        sidecar.write_bytes((failpoint + "\n").encode("ascii"))
    return result


def file_tree(path: Path) -> tuple[tuple[str, str, bytes], ...]:
    rows: list[tuple[str, str, bytes]] = []
    if not path.exists():
        return ()
    for item in sorted(path.rglob("*")):
        relative = item.relative_to(path).as_posix()
        if item.is_symlink():
            rows.append((relative, "symlink", os.readlink(item).encode()))
        elif item.is_dir():
            rows.append((relative, "dir", b""))
        elif item.is_file():
            rows.append((relative, "file", item.read_bytes()))
        else:
            rows.append((relative, "other", b""))
    return tuple(rows)
