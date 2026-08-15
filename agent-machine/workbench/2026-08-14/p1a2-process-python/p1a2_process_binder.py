"""Read-only P0 evidence binder for the Phase 2 process leaf."""
from __future__ import annotations

import hashlib
import os
import stat
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from p1a2_model import _termination
from p1a2_process_contract import RAW_NAMES, process_call
from p1a2_process_raw import validate_prefix
from p1a2_store import Corruption, MAX_DIRECTORY_ENTRIES, MAX_JSON, canonical_json, exact_fields, integer, is_temp_for, list_directory, make_ref, read_regular, strict_json, validate_ref

ATTEMPT = "attempt-1"
OPTIONAL = ("receipt-ready.json", "terminal.json")
ORDER = ("request.json", "stdin.bin", "request-ready.json", "dispatch-intent.json", "stdout.bin", "stderr.bin", "receipt.json", *OPTIONAL)


@dataclass(frozen=True)
class KnownEvidence:
    invocation_id: str
    termination: dict[str, Any]
    stdout: bytes
    stderr: bytes
    evidence_hash: str


@dataclass(frozen=True)
class IncompleteUnknown:
    attempts_ref: dict[str, Any]


def _dir(path: Path, label: str, *, optional: bool = False) -> bool:
    try:
        info = os.lstat(path)
    except FileNotFoundError:
        if optional:
            return False
        raise Corruption(label + ": missing directory")
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
        raise Corruption(label + ": immediate non-symlink directory required")
    return True


def _uuid(value: str) -> bool:
    return len(value) == 32 and all(char in "0123456789abcdef" for char in value)


def _tree_ref(attempts: Path, folder: Path | None) -> dict[str, Any]:
    if not _dir(attempts, "attempts", optional=True):
        return make_ref(canonical_json({"present": False, "entries": []}))
    rows: list[dict[str, Any]] = []
    attempt, p0 = attempts / ATTEMPT, attempts / ATTEMPT / "p0"
    for path, rel in ((attempt, ATTEMPT), (p0, ATTEMPT + "/p0")):
        if _dir(path, "attempts tree", optional=True): rows.append({"kind": "dir", "path": rel})
    if folder is not None:
        rows.append({"kind": "dir", "path": ATTEMPT + "/p0/" + folder.name})
        for entry in list_directory(folder, "attempts tree"):
            data = read_regular(folder / entry.name, 8 * 1024 * 1024 if ".bin" in entry.name else MAX_JSON, "attempts tree file")
            assert data is not None
            rows.append({"kind": "file", "path": ATTEMPT + "/p0/" + folder.name + "/" + entry.name, "ref": make_ref(data)})
    rows.sort(key=lambda row: row["path"].encode())
    return make_ref(canonical_json({"present": True, "entries": rows}))


def _json(path: Path, label: str) -> dict[str, Any]:
    data = read_regular(path, MAX_JSON, label)
    assert data is not None
    value = strict_json(data, label)
    if not isinstance(value, dict):
        raise Corruption(label + ": object required")
    return value


def _p0_dir(attempts: Path) -> tuple[Path | None, str | None]:
    if not _dir(attempts, "attempts", optional=True):
        return None, None
    names = {entry.name for entry in list_directory(attempts, "attempts")}
    if names - {ATTEMPT}:
        raise Corruption("other attempt entry is a contradiction")
    attempt = attempts / ATTEMPT
    if not _dir(attempt, "attempt-1", optional=True):
        return None, None
    names = {entry.name for entry in list_directory(attempt, "attempt-1")}
    if names - {"p0"}:
        raise Corruption("attempt-1 entry is a contradiction")
    p0 = attempt / "p0"
    if not _dir(p0, "p0", optional=True):
        return None, None
    entries = list_directory(p0, "p0")
    if len(entries) > 1 or any(not _uuid(entry.name) or entry.is_symlink() or not entry.is_dir(follow_symlinks=False) for entry in entries):
        raise Corruption("P0 UUID set is a contradiction")
    if not entries:
        return None, None
    return p0 / entries[0].name, entries[0].name


def _validate_files(folder: Path) -> set[str]:
    allowed = set(RAW_NAMES + OPTIONAL)
    present: set[str] = set()
    for entry in list_directory(folder, "P0 UUID"):
        if entry.name in allowed:
            read_regular(folder / entry.name, 8 * 1024 * 1024 if entry.name.endswith(".bin") else MAX_JSON, "P0 raw")
            present.add(entry.name)
        elif any(is_temp_for(entry.name, name) for name in allowed):
            read_regular(folder / entry.name, 8 * 1024 * 1024 if ".bin." in entry.name else MAX_JSON, "P0 atomic temp")
        else:
            raise Corruption("P0 UUID contains unknown or special entry")
    seen_gap = False
    for name in ORDER:
        if name not in present:
            seen_gap = True
        elif seen_gap:
            raise Corruption("P0 raw publish order is a contradiction")
    frontier = next((name for name in ORDER if name not in present), None)
    for entry in list_directory(folder, "P0 UUID"):
        if entry.name not in allowed:
            temp_for = next((name for name in allowed if is_temp_for(entry.name, name)), None)
            if temp_for != frontier:
                raise Corruption("P0 future atomic temp is a contradiction")
    return present


def bind(call: dict[str, Any], attempts: Path) -> KnownEvidence | IncompleteUnknown:
    """Return only known evidence or incomplete unknown; contradictions raise.

    This function intentionally does not mkdir, truncate, publish, or append.
    """
    call = process_call(call); folder, invocation = _p0_dir(attempts)
    present = _validate_files(folder) if folder is not None else set()
    snapshot = _tree_ref(attempts, folder)
    if folder is None:
        return IncompleteUnknown(snapshot)
    validate_prefix(call, folder, present)
    if not set(RAW_NAMES).issubset(present):
        return IncompleteUnknown(snapshot)
    stdin = read_regular(folder / "stdin.bin", 8 * 1024 * 1024, "stdin.bin")
    stdout = read_regular(folder / "stdout.bin", 8 * 1024 * 1024, "stdout.bin")
    stderr = read_regular(folder / "stderr.bin", 8 * 1024 * 1024, "stderr.bin")
    assert stdin is not None and stdout is not None and stderr is not None
    receipt = exact_fields(_json(folder / "receipt.json", "receipt.json"), {"version", "outcome", "stdout", "stderr"}, "receipt.json")
    if integer(receipt["version"], "receipt.version", 0, 0) != 0 or not isinstance(receipt["outcome"], dict) or receipt["outcome"].get("kind") == "outcome_unknown":
        raise Corruption("P0 outcome is a contradiction")
    term = _termination(receipt["outcome"], "P0 outcome")
    if validate_ref(receipt["stdout"], "P0 stdout") != make_ref(stdout) or validate_ref(receipt["stderr"], "P0 stderr") != make_ref(stderr):
        raise Corruption("P0 stream refs mismatch")
    if "receipt-ready.json" in present and _json(folder / "receipt-ready.json", "receipt-ready.json") != {"state": "receipt_ready"}:
        raise Corruption("P0 receipt-ready mismatch")
    if "terminal.json" in present and _json(folder / "terminal.json", "terminal.json") != {"state": "terminal", "outcome": term}:
        raise Corruption("P0 terminal mismatch")
    manifest = [{"name": name, "ref": make_ref(read_regular(folder / name, 8 * 1024 * 1024 if name.endswith(".bin") else MAX_JSON, name) or b"")} for name in RAW_NAMES]
    evidence = hashlib.sha256(b"aos-p1a2-process-evidence/v1\0" + canonical_json(manifest)).hexdigest()
    assert invocation is not None
    return KnownEvidence(invocation, term, stdout, stderr, evidence)
