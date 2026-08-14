"""Store-neutral strict bytes/JSON primitives for the P1a workbench.

No Task state lives here.  `p1a_model` owns the fixture grammar and replay
state machine; this module owns canonical encoding and fail-closed decoding.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
from pathlib import Path
from typing import Any

ID_RE = re.compile(r"^[0-9a-f]{64}$")


class StoreError(RuntimeError):
    pass


class Corruption(StoreError):
    pass


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(obj: Any) -> bytes:
    return json.dumps(obj, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def strict_json(data: bytes, label: str) -> Any:
    def bad_constant(value: str) -> None:
        raise ValueError("non-finite number: " + value)

    def no_dupes(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        out: dict[str, Any] = {}
        for key, value in pairs:
            if key in out:
                raise ValueError("duplicate key: " + key)
            out[key] = value
        return out
    try:
        return json.loads(data.decode("utf-8"), object_pairs_hook=no_dupes,
                          parse_constant=bad_constant)
    except (UnicodeDecodeError, ValueError, json.JSONDecodeError) as exc:
        raise Corruption("bad strict json: " + label) from exc


def exact(obj: dict[str, Any], keys: set[str], label: str) -> None:
    if set(obj) != keys:
        raise Corruption(f"{label}: unexpected/missing fields {sorted(set(obj) ^ keys)}")


def text(value: Any, label: str) -> str:
    if not isinstance(value, str):
        raise Corruption(label + " must be string")
    return value


def task_id(value: Any, label: str = "task_id") -> str:
    value = text(value, label)
    if not ID_RE.fullmatch(value):
        raise Corruption(label + " is not a 64 lowercase hex id")
    return value


def fsync_dir(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)
