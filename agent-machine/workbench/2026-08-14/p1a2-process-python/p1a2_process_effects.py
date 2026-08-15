"""Phase 2 authority/effect transitions, separated from read-only projection."""
from __future__ import annotations

from typing import Any

from p1a2_model import CALL, EVENTS, TASK, _task_bytes
from p1a2_process_binder import IncompleteUnknown, KnownEvidence, bind
from p1a2_process_contract import child_id, fake_call, fake_receipt, live_generation, materialize_first, process_receipt
from p1a2_process_state import ATTEMPT, ATTEMPTS
from p1a2_store import Corruption, MAX_JSON, canonical_json, list_directory, make_ref, read_regular, require_directory

PAYLOAD = "payload"


def plan(runtime: Any, view: dict[str, Any], slot: str) -> bool:
    ident = child_id(view["root"], slot); folder = runtime.paths.tasks / ident
    if not folder.exists(): runtime._mkdir(folder); return True
    require_directory(folder, "child staging")
    recipe = materialize_first(view["call"]) if slot == "first" else fake_call(view["call"]); data = canonical_json(recipe); call = folder / CALL
    if read_regular(call, MAX_JSON, "staged Call", required=False) != data:
        runtime._entries(folder, {CALL}, "child staging"); runtime._publish(call, data, staging=True); return True
    runtime._append(runtime.paths.tasks / view["root"] / EVENTS, {"v": 1, "type": "child_planned", "slot": slot, "task_id": ident, "call_ref": make_ref(data)})
    return True


def process(runtime: Any, child: dict[str, Any]) -> bool:
    stage = child["stage"]; folder = child["folder"]
    if stage == "planned": runtime._publish(folder / TASK, _task_bytes(child["id"], child["ref"])); return True
    if stage == "materialized": runtime._append(folder / EVENTS, {"v": 1, "type": "accepted"}); return True
    if stage == "accepted": runtime._append(runtime.paths.tasks / child["root"] / EVENTS, {"v": 1, "type": "child_linked", "slot": "first", "task_id": child["id"], "call_ref": child["ref"]}); return True
    if stage == "linked":
        attempts = folder / ATTEMPTS
        if attempts.exists() and list_directory(attempts, "pre-intent attempts"): raise Corruption("attempt exists before intent")
        try: current = live_generation(child["call"]["definition"]["absolute_path"])
        except Corruption: current = None
        if current != child["call"]["definition"]["generation"]["hex"]:
            runtime._append(folder / EVENTS, {"v": 1, "type": "repair_required", "reason": "generation"}); return True
        runtime._append(folder / EVENTS, {"v": 1, "type": "dispatch_intent", "attempt_id": ATTEMPT}); runtime._run_p0(child); return True
    if stage != "intent": return False
    evidence = bind(child["call"], folder / ATTEMPTS)
    if isinstance(evidence, IncompleteUnknown):
        runtime._append(folder / EVENTS, {"v": 1, "type": "repair_required", "reason": "incomplete_evidence", "attempts_ref": evidence.attempts_ref}); return True
    assert isinstance(evidence, KnownEvidence)
    receipt = process_receipt(child["id"], child["ref"], evidence.invocation_id, evidence.evidence_hash, evidence.termination, evidence.stdout, evidence.stderr); path = folder / PAYLOAD
    if not path.exists(): runtime._mkdir(path); return True
    final = path / (make_ref(receipt)["sha256"] + ".receipt.json")
    if not final.exists(): runtime._publish(final, receipt); return True
    runtime._append(folder / EVENTS, {"v": 1, "type": "receipt_committed", "receipt_ref": make_ref(receipt)}); return True


def fake(runtime: Any, child: dict[str, Any]) -> bool:
    folder = child["folder"]
    if child["stage"] == "planned": runtime._publish(folder / TASK, _task_bytes(child["id"], child["ref"])); return True
    if child["stage"] == "materialized": runtime._append(folder / EVENTS, {"v": 1, "type": "accepted"}); return True
    if child["stage"] == "accepted": runtime._append(runtime.paths.tasks / child["root"] / EVENTS, {"v": 1, "type": "child_linked", "slot": "second", "task_id": child["id"], "call_ref": child["ref"]}); return True
    if child["stage"] not in ("linked", "receipt"): return False
    receipt = fake_receipt(child["id"], child["ref"], child["call"]["result"]); path = folder / PAYLOAD
    if not path.exists(): runtime._mkdir(path); return True
    final = path / (make_ref(receipt)["sha256"] + ".receipt.json")
    if not final.exists(): runtime._publish(final, receipt); return True
    if child["stage"] == "linked": runtime._append(folder / EVENTS, {"v": 1, "type": "receipt_committed", "receipt_ref": make_ref(receipt)}); return True
    return False


def step(runtime: Any, view: dict[str, Any]) -> bool:
    first, second = view["first"], view["second"]; root_events = runtime.paths.tasks / view["root"] / EVENTS
    if first["stage"] == "none": return plan(runtime, view, "first")
    if first["stage"] in ("planned", "materialized", "accepted", "linked", "intent"): return process(runtime, first)
    if first["stage"] == "receipt": runtime._append(root_events, {"v": 1, "type": "child_observed", "slot": "first", "task_id": first["id"], "receipt_ref": first["receipt_ref"]}); return True
    if first["stage"] != "observed" or not view["oracle_ok"]: return False
    if second["stage"] == "none": return plan(runtime, view, "second")
    if second["stage"] in ("planned", "materialized", "accepted", "linked"): return fake(runtime, second)
    if second["stage"] == "receipt": runtime._append(root_events, {"v": 1, "type": "child_observed", "slot": "second", "task_id": second["id"], "receipt_ref": second["receipt_ref"]}); return True
    if second["stage"] != "observed" or view["committed"]: return False
    path = runtime.paths.tasks / view["root"] / PAYLOAD; receipt = view["receipt"]
    if not path.exists(): runtime._mkdir(path); return True
    final = path / (make_ref(receipt)["sha256"] + ".receipt.json")
    if not final.exists(): runtime._publish(final, receipt); return True
    runtime._append(root_events, {"v": 1, "type": "receipt_committed", "receipt_ref": make_ref(receipt)}); return True
