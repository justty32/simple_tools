#!/usr/bin/env python3
"""CTest driver: compare the C++ adapter against the Python walking oracle."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--runner", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--python-root", required=True)
    ns = ap.parse_args()
    sys.path.insert(0, ns.python_root)
    from aos_p0 import FunctionStore, Request
    root = Path(tempfile.mkdtemp(prefix="aos-p0-cpp-", dir="/tmp"))
    try:
        def one(name: str, args: list[str], stdin: bytes = b"", cwd: str | None = None) -> None:
            cpp = root / (name + "-cpp")
            source = root / (name + ".stdin")
            source.write_bytes(stdin)
            cmd = [ns.runner, "--out-dir", str(cpp), "--stdin", str(source)]
            if cwd is not None:
                cmd += ["--cwd", cwd]
            cmd += ["--", ns.fixture, *args]
            got = json.loads(subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout)
            store = FunctionStore(root / (name + "-python"))
            ident, expected = store.run(Request(ns.fixture, [ns.fixture, *args], stdin, cwd))
            py = store.root / ident
            # Python adds signal_name; the common process mechanics are kind/code/signal.
            common = {k: v for k, v in expected["outcome"].items() if k in {"kind", "code", "signal"}}
            assert got["outcome"] == common, (name, got, expected)
            assert (cpp / "stdout.bin").read_bytes() == (py / "stdout.bin").read_bytes(), name + " stdout"
            assert (cpp / "stderr.bin").read_bytes() == (py / "stderr.bin").read_bytes(), name + " stderr"
            assert got["stdout"]["size"] == expected["stdout"]["size"]
            assert got["stderr"]["size"] == expected["stderr"]["size"]

        one("argv", ["argv", "", "two words", "*.txt", "$(whoami)", ";not-shell", "$HOME"])
        one("binary", ["echo"], b"prefix\0\xff\xfe\x80suffix")
        one("dual", ["split", "500000"])
        one("signal", ["sigterm"])
        one("sigpipe-default", ["sigpipe-default"])
        chosen = root / "chosen-cwd"; chosen.mkdir()
        one("cwd", ["cwd"], cwd=str(chosen))
        # 16 MiB plus a delayed reader catches a sequential stdin/stdout implementation.
        one("large-delay", ["delay-read", "40"], os.urandom(16 * 1024 * 1024))
        # Fixture closes stdin without consuming it: parent must survive EPIPE.
        one("parent-epipe", ["argv", "closes-stdin"], os.urandom(1024 * 1024))
        for n in range(12):
            one("repeat-" + str(n), ["split", "131072"])

        def launch_case(label: str, executable: str, expected_errno: int) -> None:
            out = root / label; inp = root / (label + ".stdin"); inp.write_bytes(b"")
            received = json.loads(subprocess.run([ns.runner, "--out-dir", str(out), "--stdin", str(inp), "--", executable], check=True, stdout=subprocess.PIPE).stdout)
            store = FunctionStore(root / (label + "-py")); _, oracle = store.run(Request(executable, [executable], b""))
            assert received["outcome"]["kind"] == "launch_error" and received["outcome"]["errno"] == expected_errno, received
            assert oracle["outcome"]["kind"] == "spawn_error" and oracle["outcome"]["errno"] == expected_errno, oracle
        launch_case("enoent", "/definitely/not/a/program", 2)
        blocked = root / "not-executable"; blocked.write_text("not executable")
        blocked.chmod(0o600)
        launch_case("eacces", str(blocked), 13)
        bad_cwd = root / "missing-cwd"
        out = root / "bad-cwd-cpp"; inp = root / "bad-cwd.stdin"; inp.write_bytes(b"")
        received = json.loads(subprocess.run([ns.runner, "--out-dir", str(out), "--stdin", str(inp), "--cwd", str(bad_cwd), "--", ns.fixture, "cwd"], check=True, stdout=subprocess.PIPE).stdout)
        store = FunctionStore(root / "bad-cwd-py")
        _, oracle = store.run(Request(ns.fixture, [ns.fixture, "cwd"], b"", str(bad_cwd)))
        assert received["outcome"]["kind"] == "launch_error" and received["outcome"]["stage"] == "chdir" and received["outcome"]["errno"] == 2, received
        assert oracle["outcome"]["kind"] == "spawn_error" and oracle["outcome"]["errno"] == 2, oracle
        print("differential: argv/binary/dual/exit/signal/SIGPIPE/cwd/16MiB/EPIPE/ENOENT/EACCES/chdir passed")
        return 0
    finally:
        shutil.rmtree(root, ignore_errors=True)

if __name__ == "__main__":
    raise SystemExit(main())
