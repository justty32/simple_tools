"""A disposable walking prototype for one AOS Function call on Linux."""

from __future__ import annotations

import argparse
import base64
import errno as errno_module
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import uuid
from dataclasses import dataclass
from typing import Any


class SimulatedCrash(RuntimeError):
    """Test-only interruption point; it intentionally leaves an invocation open."""


@dataclass(frozen=True)
class Request:
    executable: str
    argv: list[str]
    stdin: bytes = b""
    cwd: str | None = None

    def as_json(self) -> dict[str, Any]:
        return {
            "executable": self.executable,
            "argv": self.argv,
            "cwd": self.cwd,
            "stdin_encoding": "stored-separately-as-raw-bytes",
        }


def _digest(data: bytes) -> dict[str, Any]:
    return {"sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


class FunctionStore:
    """Filesystem-backed records for exactly one complete process invocation."""

    _INVOCATION_ID = re.compile(r"[0-9a-f]{32}")

    def __init__(self, root: str | Path):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self._fsync_dir(self.root)

    @staticmethod
    def _fsync_dir(path: Path) -> None:
        fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)

    @staticmethod
    def _json_bytes(value: Any) -> bytes:
        return (json.dumps(value, sort_keys=True, ensure_ascii=False, indent=2) + "\n").encode("utf-8")

    def _publish_bytes(self, directory: Path, name: str, data: bytes) -> None:
        fd, temporary = tempfile.mkstemp(prefix="." + name + ".", dir=directory)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, directory / name)
            self._fsync_dir(directory)
        except BaseException:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass
            raise

    def _publish_json(self, directory: Path, name: str, value: Any) -> None:
        self._publish_bytes(directory, name, self._json_bytes(value))

    def _new_invocation(self) -> tuple[str, Path]:
        invocation_id = uuid.uuid4().hex
        directory = self._invocation_directory(invocation_id)
        directory.mkdir()
        self._fsync_dir(self.root)
        self._fsync_dir(directory)
        return invocation_id, directory

    def _invocation_directory(self, invocation_id: str, *, must_exist: bool = False) -> Path:
        """Map only prototype-generated IDs to an immediate, non-symlink child of root."""
        if not isinstance(invocation_id, str) or self._INVOCATION_ID.fullmatch(invocation_id) is None:
            raise ValueError("invalid invocation id: expected 32 lowercase hexadecimal characters")
        directory = self.root / invocation_id
        if directory.parent != self.root:
            raise ValueError("invocation path is not a direct child of root")
        if must_exist:
            if directory.is_symlink():
                raise ValueError("invocation directory symlinks are unsupported in this prototype")
            if not directory.is_dir():
                raise FileNotFoundError(invocation_id)
        return directory

    def _persist_request(self, directory: Path, request: Request) -> None:
        self._publish_json(directory, "request.json", request.as_json())
        self._publish_bytes(directory, "stdin.bin", request.stdin)
        self._publish_json(directory, "request-ready.json", {"state": "request_ready"})

    def _intent(self, directory: Path) -> None:
        self._publish_json(directory, "dispatch-intent.json", {"state": "dispatch_intent"})

    def _save_result(self, directory: Path, result: dict[str, Any], stdout: bytes, stderr: bytes, *, publish_ready: bool = True) -> None:
        self._publish_bytes(directory, "stdout.bin", stdout)
        self._publish_bytes(directory, "stderr.bin", stderr)
        self._publish_json(directory, "receipt.json", result)
        if publish_ready:
            self._publish_json(directory, "receipt-ready.json", {"state": "receipt_ready"})

    def _terminal(self, directory: Path, result: dict[str, Any]) -> None:
        self._publish_json(directory, "terminal.json", {"state": "terminal", "outcome": result["outcome"]})

    @staticmethod
    def _result(returncode: int | None, stdout: bytes, stderr: bytes, spawn_error: OSError | None = None) -> dict[str, Any]:
        if spawn_error is not None:
            outcome: dict[str, Any] = {
                "kind": "spawn_error", "stage": "spawn", "errno": spawn_error.errno,
                "errno_name": errno_module.errorcode.get(spawn_error.errno, "UNKNOWN"),
            }
        elif returncode is not None and returncode < 0:
            outcome = {"kind": "signaled", "signal": -returncode, "signal_name": signal.Signals(-returncode).name}
        else:
            outcome = {"kind": "exited", "code": returncode}
        return {"version": 0, "outcome": outcome, "stdout": _digest(stdout), "stderr": _digest(stderr)}

    def run(self, request: Request, *, crash_at: str | None = None) -> tuple[str, dict[str, Any]]:
        if not request.executable:
            raise ValueError("executable must be non-empty")
        if any("\x00" in item for item in request.argv):
            raise ValueError("argv cannot contain NUL")
        invocation_id, directory = self._new_invocation()
        self._persist_request(directory, request)
        self._intent(directory)
        if crash_at == "after_intent":
            raise SimulatedCrash(invocation_id)
        try:
            # communicate is the Python oracle here: it concurrently supplies stdin and drains both pipes.
            process = subprocess.Popen(
                request.argv, executable=request.executable, stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=request.cwd,
                shell=False,
            )
            stdout, stderr = process.communicate(request.stdin)
            result = self._result(process.returncode, stdout, stderr)
        except OSError as error:
            stdout, stderr = b"", b""
            result = self._result(None, stdout, stderr, error)
        self._save_result(directory, result, stdout, stderr, publish_ready=crash_at != "after_receipt_json")
        if crash_at == "after_receipt_json":
            raise SimulatedCrash(invocation_id)
        if crash_at == "after_receipt":
            raise SimulatedCrash(invocation_id)
        self._terminal(directory, result)
        return invocation_id, result

    @staticmethod
    def _is_int(value: Any) -> bool:
        return isinstance(value, int) and not isinstance(value, bool)

    def _validate_request_and_intent(self, directory: Path) -> str | None:
        required = {"request.json", "stdin.bin", "request-ready.json", "dispatch-intent.json"}
        missing = sorted(name for name in required if not (directory / name).is_file())
        if missing:
            return "missing request/intent artifacts: " + ", ".join(missing)
        try:
            request = json.loads((directory / "request.json").read_text(encoding="utf-8"))
            ready = json.loads((directory / "request-ready.json").read_text(encoding="utf-8"))
            intent = json.loads((directory / "dispatch-intent.json").read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            return "invalid request/intent JSON: " + type(error).__name__
        if not isinstance(request, dict) or not isinstance(request.get("executable"), str):
            return "invalid request executable"
        if not isinstance(request.get("argv"), list) or not all(isinstance(item, str) for item in request["argv"]):
            return "invalid request argv"
        if request.get("cwd") is not None and not isinstance(request.get("cwd"), str):
            return "invalid request cwd"
        if request.get("stdin_encoding") != "stored-separately-as-raw-bytes":
            return "invalid request stdin encoding"
        if ready != {"state": "request_ready"} or intent != {"state": "dispatch_intent"}:
            return "invalid request-ready or dispatch-intent marker"
        return None

    def _validate_receipt(self, directory: Path) -> tuple[dict[str, Any] | None, str | None]:
        relation_error = self._validate_request_and_intent(directory)
        if relation_error:
            return None, relation_error
        try:
            receipt = json.loads((directory / "receipt.json").read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            return None, "invalid receipt JSON: " + type(error).__name__
        if not isinstance(receipt, dict) or receipt.get("version") != 0 or not isinstance(receipt.get("outcome"), dict):
            return None, "invalid receipt structure"
        outcome = receipt["outcome"]
        kind = outcome.get("kind")
        if kind == "exited":
            outcome_valid = self._is_int(outcome.get("code"))
        elif kind == "signaled":
            outcome_valid = self._is_int(outcome.get("signal")) and outcome["signal"] > 0 and isinstance(outcome.get("signal_name"), str)
        elif kind == "spawn_error":
            outcome_valid = isinstance(outcome.get("stage"), str) and self._is_int(outcome.get("errno")) and isinstance(outcome.get("errno_name"), str)
        elif kind == "outcome_unknown":
            outcome_valid = isinstance(outcome.get("reason"), str) and receipt.get("stdout") is None and receipt.get("stderr") is None
        else:
            outcome_valid = False
        if not outcome_valid:
            return None, "invalid receipt outcome"
        if kind == "outcome_unknown":
            return receipt, None
        for stream in ("stdout", "stderr"):
            metadata = receipt.get(stream)
            path = directory / (stream + ".bin")
            if not isinstance(metadata, dict) or not self._is_int(metadata.get("size")) or metadata["size"] < 0:
                return None, "invalid " + stream + " metadata"
            digest = metadata.get("sha256")
            if not isinstance(digest, str) or len(digest) != 64 or any(char not in "0123456789abcdef" for char in digest):
                return None, "invalid " + stream + " hash"
            try:
                actual = path.read_bytes()
            except OSError as error:
                return None, "missing/unreadable " + stream + ": " + type(error).__name__
            if _digest(actual) != metadata:
                return None, "mismatched " + stream + " bytes"
        return receipt, None

    def _quarantine_untrusted(self, directory: Path, reason: str) -> None:
        """Keep non-authoritative result remnants, but make them untrustworthy by location."""
        quarantine = directory / "quarantine"
        quarantine.mkdir(exist_ok=True)
        self._fsync_dir(quarantine)
        candidates = {"stdout.bin", "stderr.bin", "receipt.json", "receipt-ready.json", "terminal.json"}
        candidates.update(entry.name for entry in directory.iterdir() if entry.is_file() and entry.name.startswith("."))
        moved: list[str] = []
        for name in sorted(candidates):
            source = directory / name
            if not source.is_file():
                continue
            destination = quarantine / name
            if destination.exists():
                destination = quarantine / (name + "." + uuid.uuid4().hex)
            os.replace(source, destination)
            moved.append(destination.name)
        self._fsync_dir(quarantine)
        self._fsync_dir(directory)
        self._publish_json(directory, "quarantine.json", {"reason": reason, "artifacts": moved})

    def inspect(self, invocation_id: str) -> dict[str, Any]:
        directory = self._invocation_directory(invocation_id, must_exist=True)
        names = {entry.name for entry in directory.iterdir()}
        receipt: dict[str, Any] | None = None
        validation_error: str | None = None
        if "receipt.json" in names:
            receipt, validation_error = self._validate_receipt(directory)
        quarantined: dict[str, Any] | None = None
        if "quarantine.json" in names:
            try:
                quarantined = json.loads((directory / "quarantine.json").read_text(encoding="utf-8"))
            except (OSError, UnicodeDecodeError, json.JSONDecodeError):
                quarantined = {"reason": "invalid quarantine manifest"}
        return {
            "id": invocation_id,
            "request_ready": "request-ready.json" in names,
            "dispatch_intent": "dispatch-intent.json" in names,
            "receipt_complete": receipt is not None and "receipt-ready.json" in names,
            "receipt_valid": receipt is not None,
            "receipt_validation_error": validation_error,
            "terminal": "terminal.json" in names,
            "receipt": receipt,
            "quarantine": quarantined,
        }

    def recover(self, invocation_id: str) -> dict[str, Any]:
        state = self.inspect(invocation_id)
        directory = self._invocation_directory(invocation_id, must_exist=True)
        receipt, validation_error = self._validate_receipt(directory)
        if receipt is not None:
            if not (directory / "receipt-ready.json").is_file():
                self._publish_json(directory, "receipt-ready.json", {"state": "receipt_ready"})
            if not state["terminal"]:
                self._terminal(directory, receipt)
            return self.inspect(invocation_id)
        if state["dispatch_intent"]:
            # An attempted process could have side effects. Deliberately never re-run it.
            self._quarantine_untrusted(directory, validation_error or "missing receipt")
            unknown = {
                "version": 0,
                "outcome": {"kind": "outcome_unknown", "reason": "dispatch intent exists without complete receipt"},
                "stdout": None,
                "stderr": None,
            }
            self._publish_json(directory, "receipt.json", unknown)
            self._publish_json(directory, "receipt-ready.json", {"state": "receipt_ready"})
            self._terminal(directory, unknown)
        return self.inspect(invocation_id)


def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="Linux filesystem directory holding invocations")
    commands = parser.add_subparsers(dest="command", required=True)
    run_parser = commands.add_parser("run")
    run_parser.add_argument("--stdin-base64", default="")
    run_parser.add_argument("--cwd")
    run_parser.add_argument("program_and_args", nargs=argparse.REMAINDER)
    for name in ("inspect", "recover"):
        commands.add_parser(name).add_argument("invocation_id")
    args = parser.parse_args()
    store = FunctionStore(args.root)
    if args.command == "run":
        if not args.program_and_args or args.program_and_args[0] != "--" or len(args.program_and_args) < 2:
            parser.error("run requires: run [options] -- PROGRAM ARG...")
        program, *rest = args.program_and_args[1:]
        invocation_id, receipt = store.run(Request(program, [program, *rest], base64.b64decode(args.stdin_base64), args.cwd))
        print(json.dumps({"id": invocation_id, "receipt": receipt}, ensure_ascii=False))
    elif args.command == "inspect":
        print(json.dumps(store.inspect(args.invocation_id), ensure_ascii=False))
    else:
        print(json.dumps(store.recover(args.invocation_id), ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
