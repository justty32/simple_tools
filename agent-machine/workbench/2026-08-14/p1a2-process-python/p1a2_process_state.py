"""Read-only reachable projection for the Phase 2 process/fake fixture."""
from __future__ import annotations

import os
from typing import Any

from p1a2_model import CALL, EVENTS, TASK, _validate_task, _bytes_result
from p1a2_process_binder import IncompleteUnknown, KnownEvidence, bind
from p1a2_process_contract import bind_frozen_first, child_id, composite_receipt, fake_call, process_call, process_receipt
from p1a2_process_projection import PAYLOAD, fake_child, payload, root, same
from p1a2_store import Corruption, MAX_JSON, canonical_json, make_ref, read_json_log, read_regular, strict_json, validate_ref

ATTEMPTS = "attempts"
ATTEMPT = "attempt-1"


def child(runtime: Any, root_id: str, slot: str, root_call: dict[str, Any], events: tuple[dict[str, Any], ...], index: int) -> tuple[dict[str, Any], int]:
    ident = child_id(root_id, slot)
    if index == len(events) or events[index].get("type") != "child_planned": return {"id": ident, "stage": "none"}, index
    event = events[index]
    try: event_ref = validate_ref(event.get("call_ref"), "child_planned.call_ref")
    except Corruption: raise
    same(event, {"v": 1, "type": "child_planned", "slot": slot, "task_id": ident, "call_ref": event_ref}, "child_planned")
    folder = runtime._folder(ident); allowed = {CALL, TASK, EVENTS, PAYLOAD} | ({ATTEMPTS} if slot == "first" else set()); names = runtime._entries(folder, allowed, slot + " Task")
    data = read_regular(folder / CALL, MAX_JSON, slot + " Call"); assert data is not None
    value = bind_frozen_first(root_call, process_call(strict_json(data, slot + " Call"))) if slot == "first" else fake_call(root_call)
    if data != canonical_json(value) or make_ref(data) != event_ref: raise Corruption(slot + " Call/ref mismatch")
    ref = make_ref(data); index += 1; linked = index < len(events) and events[index].get("type") == "child_linked"
    if linked: same(events[index], {"v": 1, "type": "child_linked", "slot": slot, "task_id": ident, "call_ref": ref}, "child_linked"); index += 1
    task = read_regular(folder / TASK, MAX_JSON, slot + " task", required=False); log = read_json_log(folder / EVENTS, slot + " events", required=False)
    if linked and task is None: raise Corruption("linked child lacks task")
    if task is not None: _validate_task(task, ident, ref)
    common = {"root": root_id, "id": ident, "folder": folder, "call": value, "ref": ref, "parent_linked": linked, "payload_present": PAYLOAD in names, "tails": (log.tail,)}
    if not log.events:
        if PAYLOAD in names or (slot == "first" and os.path.lexists(folder / ATTEMPTS)): raise Corruption("child has effect artifacts before accepted")
        return {**common, "stage": "planned" if task is None else "materialized"}, index
    same(log.events[0], {"v": 1, "type": "accepted"}, slot + " accepted")
    return fake_child(common, events, index, log.events) if slot == "second" else process_child(common, events, index, log.events)


def process_child(common: dict[str, Any], root_events: tuple[dict[str, Any], ...], index: int, events: tuple[dict[str, Any], ...]) -> tuple[dict[str, Any], int]:
    if len(events) > 3: raise Corruption("process leaf event overflow")
    repair_generation = len(events) == 2 and events[1].get("type") == "repair_required" and events[1].get("reason") == "generation"
    if len(events) > 1 and not common["parent_linked"]: raise Corruption("unlinked process has intent")
    if len(events) > 1 and not repair_generation: same(events[1], {"v": 1, "type": "dispatch_intent", "attempt_id": ATTEMPT}, "dispatch_intent")
    if repair_generation:
        same(events[1], {"v": 1, "type": "repair_required", "reason": "generation"}, "generation repair")
        if os.path.lexists(common["folder"] / ATTEMPTS): raise Corruption("generation repair has attempts")
        if common["payload_present"]: raise Corruption("generation repair has payload")
        return {**common, "stage": "repair_generation", "evidence": None, "receipt": None, "receipt_ref": None, "payload": None}, index
    state = "intent" if len(events) > 1 else "linked" if common["parent_linked"] else "accepted"; evidence = bind(common["call"], common["folder"] / ATTEMPTS) if state == "intent" else None
    if state != "intent" and (common["payload_present"] or os.path.lexists(common["folder"] / ATTEMPTS)): raise Corruption("process has effect artifacts before intent")
    if len(events) == 3 and not isinstance(evidence, KnownEvidence):
        repair = events[2]
        if isinstance(evidence, IncompleteUnknown): same(repair, {"v": 1, "type": "repair_required", "reason": "incomplete_evidence", "attempts_ref": evidence.attempts_ref}, "incomplete repair"); state = "repair_incomplete"
        else: raise Corruption("process terminal event mismatch")
    receipt = receipt_ref = payload_value = None
    if not isinstance(evidence, KnownEvidence) and common["payload_present"]: raise Corruption("unknown process has payload")
    if isinstance(evidence, KnownEvidence):
        receipt = process_receipt(common["id"], common["ref"], evidence.invocation_id, evidence.evidence_hash, evidence.termination, evidence.stdout, evidence.stderr); receipt_ref = make_ref(receipt)
        if len(events) == 3 and events[2].get("type") == "receipt_committed": same(events[2], {"v": 1, "type": "receipt_committed", "receipt_ref": receipt_ref}, "process receipt"); state = "receipt"
        elif len(events) == 3: raise Corruption("process terminal event mismatch")
        payload_value = payload(common["folder"], receipt, state == "receipt")
    observed = index < len(root_events) and root_events[index].get("type") == "child_observed"
    if observed:
        if state != "receipt": raise Corruption("observed unfinished process")
        same(root_events[index], {"v": 1, "type": "child_observed", "slot": "first", "task_id": common["id"], "receipt_ref": receipt_ref}, "child_observed"); index += 1; state = "observed"
    return {**common, "stage": state, "evidence": evidence, "receipt": receipt, "receipt_ref": receipt_ref, "payload": payload_value}, index


def read_state(runtime: Any) -> dict[str, Any]:
    root_id, call, ref, tails, events = root(runtime); first, index = child(runtime, root_id, "first", call, events, 1)
    oracle = _bytes_result(call["first_success_oracle"], "first oracle")
    evidence = first.get("evidence"); ok = first["stage"] == "observed" and isinstance(evidence, KnownEvidence) and (evidence.termination, evidence.stdout, evidence.stderr) == oracle
    second, index = child(runtime, root_id, "second", call, events, index) if ok else ({"id": child_id(root_id, "second"), "stage": "none"}, index)
    receipt = composite_receipt(root_id, ref, first["receipt_ref"], second["receipt_ref"]) if second["stage"] == "observed" else None
    root_payload = runtime._folder(root_id) / PAYLOAD
    if receipt is None and os.path.lexists(root_payload): raise Corruption("root has premature payload")
    committed = receipt is not None and index < len(events) and events[index].get("type") == "receipt_committed"
    if committed:
        same(events[index], {"v": 1, "type": "receipt_committed", "receipt_ref": make_ref(receipt)}, "root receipt")
        index += 1; payload_value = payload(runtime._folder(root_id), receipt, True)
    else: payload_value = payload(runtime._folder(root_id), receipt, False) if receipt is not None else (False, False)
    if index != len(events): raise Corruption("root event tail mismatch")
    return {"root": root_id, "call": call, "ref": ref, "first": first, "second": second, "oracle_ok": ok, "receipt": receipt, "committed": committed, "payload": payload_value, "tails": tuple(x for x in tails + first.get("tails", ()) + second.get("tails", ()) if x is not None)}
