"""Shared, read-only root/payload/fake-leaf projection helpers."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from p1a2_model import CALL, EVENTS, ROOTS, TASK, _root_event, _validate_task, validate_root_call
from p1a2_process_contract import fake_call, fake_receipt
from p1a2_store import Corruption, MAX_JSON, canonical_json, exact_fields, integer, is_temp_for, list_directory, make_ref, read_json_log, read_regular, require_directory, strict_json, validate_ref

PAYLOAD = "payload"


def same(value: dict[str, Any], expected: dict[str, Any], label: str) -> None:
    exact_fields(value, set(expected), label); integer(value.get("v"), label + ".v", 1, 1)
    for name in ("call_ref", "receipt_ref"):
        if name in value and validate_ref(value[name], label + "." + name) != expected[name]: raise Corruption(label + " ref mismatch")
    if value != expected: raise Corruption(label + " violates exact event grammar")


def payload(folder: Path, receipt: bytes, committed: bool) -> tuple[bool, bool]:
    path = folder / PAYLOAD
    if not path.exists():
        if committed: raise Corruption("committed Receipt lacks payload directory")
        return False, False
    require_directory(path, "payload"); final = make_ref(receipt)["sha256"] + ".receipt.json"; present = False
    for entry in list_directory(path, "payload"):
        if entry.name == final:
            if read_regular(path / final, MAX_JSON, "Receipt payload") != receipt: raise Corruption("Receipt payload mismatch")
            present = True
        elif is_temp_for(entry.name, final): read_regular(path / entry.name, MAX_JSON, "Receipt payload temp")
        else: raise Corruption("unexpected payload entry")
    if committed and not present: raise Corruption("committed Receipt lacks payload")
    return True, present


def root(runtime: Any) -> tuple[str, dict[str, Any], dict[str, Any], tuple[Any, ...], tuple[dict[str, Any], ...]]:
    log = read_json_log(runtime.paths.root / ROOTS, "roots.jsonl", required=True)
    if len(log.events) != 2: raise Corruption("Phase 2 requires linked root")
    plan, link = log.events
    plan_type, ident, ref = _root_event(plan); link_type, linked_id, linked_ref = _root_event(link)
    if plan_type != "root_planned" or link_type != "root_linked" or linked_id != ident or linked_ref != ref:
        raise Corruption("root registry mismatch")
    folder = runtime._folder(ident); runtime._entries(folder, {CALL, TASK, EVENTS, PAYLOAD}, "root Task")
    data = read_regular(folder / CALL, MAX_JSON, "root Call"); assert data is not None
    if make_ref(data) != ref: raise Corruption("root Call ref mismatch")
    call = validate_root_call(strict_json(data, "root Call"))
    if canonical_json(call) != data: raise Corruption("root Call is not canonical")
    task = read_regular(folder / TASK, MAX_JSON, "root task"); assert task is not None; _validate_task(task, ident, ref)
    events = read_json_log(folder / EVENTS, "root events", required=True)
    if not events.events: raise Corruption("root lacks accepted")
    same(events.events[0], {"v": 1, "type": "accepted"}, "root accepted")
    return ident, call, ref, (log.tail, events.tail), events.events


def fake_child(common: dict[str, Any], root_events: tuple[dict[str, Any], ...], index: int, events: tuple[dict[str, Any], ...]) -> tuple[dict[str, Any], int]:
    receipt = fake_receipt(common["id"], common["ref"], common["call"]["result"]); receipt_ref = make_ref(receipt)
    expected = ({"v": 1, "type": "accepted"}, {"v": 1, "type": "receipt_committed", "receipt_ref": receipt_ref})
    if len(events) not in (1, 2): raise Corruption("fake leaf event mismatch")
    same(events[0], expected[0], "fake accepted")
    if len(events) == 2: same(events[1], expected[1], "fake receipt")
    committed = len(events) == 2; observed = index < len(root_events) and root_events[index].get("type") == "child_observed"
    if observed:
        same(root_events[index], {"v": 1, "type": "child_observed", "slot": "second", "task_id": common["id"], "receipt_ref": receipt_ref}, "child_observed")
        if not committed: raise Corruption("observed uncommitted fake")
        index += 1
    if common["payload_present"] and not common["parent_linked"]: raise Corruption("unlinked fake has payload")
    if committed and not common["parent_linked"]: raise Corruption("unlinked fake committed Receipt")
    return {**common, "stage": "observed" if observed else "receipt" if committed else "linked" if common["parent_linked"] else "accepted", "receipt": receipt, "receipt_ref": receipt_ref, "payload": payload(common["folder"], receipt, committed)}, index
