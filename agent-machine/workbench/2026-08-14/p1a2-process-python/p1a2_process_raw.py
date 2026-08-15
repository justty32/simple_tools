"""Strict validation of already-published P0 raw prefixes, with no writes."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from p1a2_model import _bytes
from p1a2_store import Corruption, MAX_JSON, exact_fields, read_regular, strict_json


def _json(path: Path, label: str) -> dict[str, Any]:
    data = read_regular(path, MAX_JSON, label); assert data is not None
    value = strict_json(data, label)
    if not isinstance(value, dict): raise Corruption(label + ": object required")
    return value


def validate_prefix(call: dict[str, Any], folder: Path, present: set[str]) -> None:
    definition = call["definition"]["absolute_path"]
    if "request.json" in present:
        request = exact_fields(_json(folder / "request.json", "request.json"), {"executable", "argv", "cwd", "stdin_encoding"}, "request.json")
        if request != {"executable": definition, "argv": call["argv"], "cwd": call["cwd"], "stdin_encoding": "stored-separately-as-raw-bytes"}:
            raise Corruption("P0 request does not bind to Call")
    if "stdin.bin" in present and read_regular(folder / "stdin.bin", 8 * 1024 * 1024, "stdin.bin") != _bytes(call["stdin"], "Call stdin"):
        raise Corruption("P0 stdin does not bind to Call")
    if "request-ready.json" in present and _json(folder / "request-ready.json", "request-ready.json") != {"state": "request_ready"}:
        raise Corruption("P0 request-ready mismatch")
    if "dispatch-intent.json" in present and _json(folder / "dispatch-intent.json", "dispatch-intent.json") != {"state": "dispatch_intent"}:
        raise Corruption("P0 dispatch marker mismatch")
