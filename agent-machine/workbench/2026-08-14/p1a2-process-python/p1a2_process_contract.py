"""Phase 2 process/fake values; this is a prototype format, not an ABI."""
from __future__ import annotations

import hashlib
import os
import stat
from pathlib import Path
from typing import Any

from p1a2_model import _bytes, _bytes_result, _path, _termination
from p1a2_store import Corruption, MAX_JSON, canonical_json, ensure_canonical_size, exact_fields, integer, make_ref, task_id, validate_ref

SLOTS = ("first", "second")
RAW_NAMES = ("request.json", "stdin.bin", "request-ready.json", "dispatch-intent.json",
             "receipt.json", "stdout.bin", "stderr.bin")


def child_id(root: str, slot: str) -> str:
    if slot not in SLOTS:
        raise Corruption("invalid child slot")
    return task_id(root + "--" + slot)


def root_recipe(root: dict[str, Any], slot: str) -> dict[str, Any]:
    if slot not in SLOTS:
        raise Corruption("invalid child slot")
    children = root["children"]
    item = exact_fields(children[SLOTS.index(slot)], {"slot", "recipe"}, "root recipe")
    if item["slot"] != slot:
        raise Corruption("root recipe order mismatch")
    return item["recipe"]


def process_call(value: Any) -> dict[str, Any]:
    ensure_canonical_size(value, MAX_JSON, "process Call")
    value = exact_fields(value, {"v", "kind", "definition", "argv", "stdin", "cwd", "environment_policy"}, "process Call")
    if integer(value["v"], "process Call.v", 1, 1) != 1 or value["kind"] != "process":
        raise Corruption("process Call v/kind mismatch")
    definition = exact_fields(value["definition"], {"absolute_path", "generation"}, "process definition")
    _path(definition["absolute_path"], "process definition path")
    generation = exact_fields(definition["generation"], {"kind", "hex"}, "process generation")
    if generation["kind"] != "sha256" or not isinstance(generation["hex"], str) or len(generation["hex"]) != 64 or any(c not in "0123456789abcdef" for c in generation["hex"]):
        raise Corruption("process generation mismatch")
    argv = value["argv"]
    if not isinstance(argv, list) or not argv or len(argv) > 256 or any(not isinstance(x, str) or "\x00" in x for x in argv):
        raise Corruption("process argv mismatch")
    _bytes(value["stdin"], "process stdin")
    _path(value["cwd"], "process cwd")
    if value["environment_policy"] != {"kind": "inherit"}:
        raise Corruption("process environment policy mismatch")
    return value


def materialize_first(root: dict[str, Any]) -> dict[str, Any]:
    recipe = exact_fields(root_recipe(root, "first"), {"kind", "definition_path", "argv", "stdin", "cwd", "environment_policy"}, "first recipe")
    if recipe["kind"] != "process" or recipe["environment_policy"] != {"kind": "inherit"}:
        raise Corruption("first recipe mismatch")
    digest = live_generation(recipe["definition_path"])
    return process_call({"v": 1, "kind": "process", "definition": {"absolute_path": recipe["definition_path"], "generation": {"kind": "sha256", "hex": digest}}, "argv": recipe["argv"], "stdin": recipe["stdin"], "cwd": recipe["cwd"], "environment_policy": recipe["environment_policy"]})


def bind_frozen_first(root: dict[str, Any], call: dict[str, Any]) -> dict[str, Any]:
    """Recheck every frozen recipe field; generation alone comes from plan time."""
    recipe = exact_fields(root_recipe(root, "first"), {"kind", "definition_path", "argv", "stdin", "cwd", "environment_policy"}, "first recipe")
    call = process_call(call)
    if (recipe["kind"] != "process" or call["definition"]["absolute_path"] != recipe["definition_path"]
            or call["argv"] != recipe["argv"] or call["stdin"] != recipe["stdin"]
            or call["cwd"] != recipe["cwd"] or call["environment_policy"] != recipe["environment_policy"]):
        raise Corruption("planned process Call bypasses frozen root recipe")
    return call


def live_generation(value: Any) -> str:
    path = Path(_path(value, "definition path"))
    try:
        info = os.lstat(path)
    except OSError as exc:
        raise Corruption("definition is unavailable") from exc
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode) or not info.st_mode & 0o111 or info.st_size > 64 * 1024 * 1024:
        raise Corruption("definition must be executable regular non-symlink")
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as exc:
        raise Corruption("definition cannot be hashed") from exc
    return digest.hexdigest()


def fake_call(root: dict[str, Any]) -> dict[str, Any]:
    recipe = exact_fields(root_recipe(root, "second"), {"kind", "result"}, "second recipe")
    if recipe["kind"] != "fake":
        raise Corruption("second recipe mismatch")
    _bytes_result(recipe["result"], "second result")
    return {"v": 1, "kind": "fake", "result": recipe["result"]}


def fake_receipt(task: str, call_ref: dict[str, Any], result: dict[str, Any]) -> bytes:
    term, stdout, stderr = _bytes_result(result, "fake result")
    value = {"v": 1, "kind": "leaf", "task_id": task_id(task), "call_ref": validate_ref(call_ref), "basis": {"kind": "fixture_fake", "slot": "second"}, "return": {"termination": term, "stdout_ref": make_ref(stdout), "stderr_ref": make_ref(stderr)}}
    return canonical_json(value)


def process_receipt(task: str, call_ref: dict[str, Any], invocation: str, evidence: str, termination: dict[str, Any], stdout: bytes, stderr: bytes) -> bytes:
    if len(invocation) != 32 or any(c not in "0123456789abcdef" for c in invocation):
        raise Corruption("invalid P0 invocation ID")
    value = {"v": 1, "kind": "leaf", "task_id": task_id(task), "call_ref": validate_ref(call_ref), "basis": {"kind": "process", "attempt_id": "attempt-1", "p0_invocation_id": invocation, "evidence_hash": evidence}, "return": {"termination": _termination(termination, "Receipt termination"), "stdout_ref": make_ref(stdout), "stderr_ref": make_ref(stderr)}}
    return canonical_json(value)


def composite_receipt(root: str, call_ref: dict[str, Any], first: dict[str, Any], second: dict[str, Any]) -> bytes:
    value = {"v": 1, "kind": "composite", "task_id": task_id(root), "call_ref": validate_ref(call_ref), "basis": {"children": [{"slot": "first", "task_id": child_id(root, "first"), "receipt_ref": validate_ref(first)}, {"slot": "second", "task_id": child_id(root, "second"), "receipt_ref": validate_ref(second)}]}, "return": {"kind": "fixture_sequence_two_success"}}
    return canonical_json(value)
