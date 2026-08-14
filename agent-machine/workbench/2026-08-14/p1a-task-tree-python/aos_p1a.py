#!/usr/bin/env python3
"""P1a-1: machine-local fake composite Task-tree experiment.

This is deliberately not an AOS implementation.  It is a small, one-writer
fault harness for the parent/child publication rules in the accompanying
README.  It never starts a Linux process for the fake leaves.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path
from typing import Any

ID_RE = re.compile(r"^[0-9a-f]{64}$")
SLOTS = ("first", "second")
PARENT_ID = "a" * 64


class StoreError(RuntimeError):
    pass


class Corruption(StoreError):
    pass


class KillAt(RuntimeError):
    pass


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(obj: Any) -> bytes:
    return json.dumps(obj, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")


def fsync_dir(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def checked_component(value: str, label: str = "id") -> str:
    if not ID_RE.fullmatch(value):
        raise StoreError(f"invalid {label}: {value!r}")
    return value


class Store:
    def __init__(self, root: Path, fail: str | None = None, armed: bool = False):
        self.root = root.resolve(strict=False)
        self.tasks = self.root / "tasks"
        self.fail = fail
        self.armed = armed
        self.fail_used = self.root / ".failpoint-used"

    def initialize(self, case: str) -> None:
        if case not in ("success", "unknown"):
            raise StoreError("case must be success or unknown")
        self.root.mkdir(parents=True, exist_ok=True)
        if self.root.is_symlink():
            raise StoreError("store root must not be symlink")
        self.tasks.mkdir(exist_ok=True)
        if self.tasks.is_symlink():
            raise StoreError("tasks directory must not be symlink")
        meta = self.root / "fixture.json"
        if meta.exists():
            if self.read_json(meta)["case"] != case:
                raise StoreError("fixture case already differs")
            return
        self.atomic_bytes(meta, canonical({"case": case, "version": 1}))
        parent = self.task_dir(PARENT_ID, create=True)
        (parent / "payload").mkdir(exist_ok=True)
        self.atomic_bytes(parent / "task.json", canonical({
            "kind": "composite", "task_id": PARENT_ID,
            "fixed_plan": list(SLOTS), "version": 1,
        }))
        self.append_event(PARENT_ID, {"type": "accepted"})

    def task_dir(self, task_id: str, create: bool = False) -> Path:
        checked_component(task_id)
        result = self.tasks / task_id
        if create:
            result.mkdir(exist_ok=True)
        if not result.exists() or not result.is_dir() or result.is_symlink():
            raise StoreError(f"unsafe or absent task directory: {task_id}")
        return result

    def atomic_bytes(self, target: Path, data: bytes) -> None:
        # target is always constructed beneath a validated direct task directory.
        parent = target.parent
        if parent.is_symlink():
            raise StoreError(f"symlink parent: {parent}")
        temp = parent / ("." + target.name + ".tmp-" + str(os.getpid()))
        fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(fd, "wb", closefd=False) as out:
                out.write(data)
                out.flush()
                os.fsync(out.fileno())
            os.close(fd)
            os.replace(temp, target)
            fsync_dir(parent)
        except Exception:
            try:
                os.close(fd)
            except OSError:
                pass
            raise

    def read_json(self, path: Path) -> dict[str, Any]:
        if path.is_symlink():
            raise StoreError(f"symlink file rejected: {path}")
        try:
            data = path.read_bytes()
            obj = json.loads(data)
        except (OSError, json.JSONDecodeError) as exc:
            raise Corruption(f"bad json: {path}") from exc
        if not isinstance(obj, dict):
            raise Corruption(f"json object expected: {path}")
        return obj

    def events(self, task_id: str) -> list[dict[str, Any]]:
        path = self.task_dir(task_id) / "events.jsonl"
        if not path.exists():
            return []
        if path.is_symlink():
            raise StoreError("symlink events rejected")
        records = []
        for line in path.read_bytes().splitlines():
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                raise Corruption(f"bad event for {task_id}") from exc
            if not isinstance(event, dict) or not isinstance(event.get("type"), str):
                raise Corruption(f"invalid event for {task_id}")
            records.append(event)
        return records

    def append_event(self, task_id: str, event: dict[str, Any]) -> None:
        path = self.task_dir(task_id) / "events.jsonl"
        data = canonical(event) + b"\n"
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        try:
            os.write(fd, data)
            os.fsync(fd)
        finally:
            os.close(fd)
        fsync_dir(path.parent)

    def hit(self, name: str) -> None:
        if self.armed and self.fail == name and not self.fail_used.exists():
            self.atomic_bytes(self.fail_used, name.encode("ascii"))
            # A real independent writer process exits here; no unwinding or cleanup.
            os._exit(97)

    def child_id(self, slot: str) -> str:
        if slot not in SLOTS:
            raise StoreError("unknown fixed-plan slot")
        return sha(b"aos-p1a-child/v1\0" + PARENT_ID.encode() + b"\0" + slot.encode())

    def child_call(self, slot: str) -> tuple[bytes, str, str]:
        child = self.child_id(slot)
        call = {
            "call_version": 1, "definition": "fixture/fake-leaf-v1",
            "fake_outcome": "unknown" if (self.case() == "unknown" and slot == "first") else "success",
            "parent_id": PARENT_ID, "slot": slot, "task_id": child,
        }
        data = canonical(call)
        digest = sha(data)
        rel = f"tasks/{PARENT_ID}/payload/{digest}.call.json"
        return data, digest, rel

    def case(self) -> str:
        return self.read_json(self.root / "fixture.json")["case"]

    def parent_events(self) -> list[dict[str, Any]]:
        return self.events(PARENT_ID)

    @staticmethod
    def one(events: list[dict[str, Any]], kind: str, slot: str | None = None) -> dict[str, Any] | None:
        found = [e for e in events if e["type"] == kind and (slot is None or e.get("slot") == slot)]
        if len(found) > 1:
            raise Corruption(f"duplicate {kind} {slot}")
        return found[0] if found else None

    def planned(self, slot: str) -> dict[str, Any] | None:
        return self.one(self.parent_events(), "child_planned", slot)

    def linked(self, slot: str) -> dict[str, Any] | None:
        return self.one(self.parent_events(), "child_linked", slot)

    def observed(self, slot: str) -> dict[str, Any] | None:
        return self.one(self.parent_events(), "child_observed", slot)

    def publish_plan(self, slot: str) -> None:
        if self.planned(slot):
            return
        data, digest, rel = self.child_call(slot)
        payload = self.task_dir(PARENT_ID) / "payload" / f"{digest}.call.json"
        if payload.exists():
            if payload.is_symlink() or sha(payload.read_bytes()) != digest:
                raise Corruption("existing parent call payload differs")
        else:
            self.hit("call_payload_before")
            self.atomic_bytes(payload, data)
            self.hit("call_payload_after")
        self.hit("planned_before")
        self.append_event(PARENT_ID, {"type": "child_planned", "slot": slot,
                                      "child_id": self.child_id(slot), "call_path": rel,
                                      "call_hash": digest})
        self.hit("planned_after")

    def validate_plan(self, slot: str, plan: dict[str, Any]) -> None:
        _, expected_hash, expected_rel = self.child_call(slot)
        if (plan.get("child_id") != self.child_id(slot)
                or plan.get("call_hash") != expected_hash
                or plan.get("call_path") != expected_rel):
            raise Corruption("planned child identity or immutable call reference differs")
        payload = self.task_dir(PARENT_ID) / "payload" / f"{expected_hash}.call.json"
        if not payload.exists() or payload.is_symlink() or sha(payload.read_bytes()) != expected_hash:
            raise Corruption("planned call payload missing or hash mismatch")

    def materialize(self, slot: str) -> None:
        plan = self.planned(slot)
        if not plan:
            return
        self.validate_plan(slot, plan)
        child = plan["child_id"]
        if child != self.child_id(slot):
            raise Corruption("non-deterministic child id")
        task = self.tasks / checked_component(child)
        if task.exists():
            if task.is_symlink() or not task.is_dir():
                raise Corruption("unsafe existing child")
            existing = self.read_json(task / "task.json")
            if existing.get("call_path") != plan["call_path"] or existing.get("call_hash") != plan["call_hash"]:
                raise Corruption("same child id has different call reference")
            return
        self.hit("materialize_before")
        task.mkdir()
        fsync_dir(self.tasks)
        self.atomic_bytes(task / "task.json", canonical({
            "kind": "fake_leaf", "task_id": child, "parent_id": PARENT_ID,
            "slot": slot, "call_path": plan["call_path"], "call_hash": plan["call_hash"],
            "version": 1,
        }))
        self.append_event(child, {"type": "genesis", "parent_id": PARENT_ID, "slot": slot,
                                  "call_path": plan["call_path"], "call_hash": plan["call_hash"]})
        self.hit("materialize_after")

    def link(self, slot: str) -> None:
        if self.linked(slot):
            return
        plan = self.planned(slot)
        if not plan or not (self.tasks / plan["child_id"] / "task.json").exists():
            return
        self.validate_plan(slot, plan)
        child = self.read_json(self.tasks / plan["child_id"] / "task.json")
        if child.get("call_path") != plan["call_path"] or child.get("call_hash") != plan["call_hash"]:
            raise Corruption("child projection disagrees with parent authority")
        self.hit("linked_before")
        self.append_event(PARENT_ID, {"type": "child_linked", "slot": slot,
                                      "child_id": plan["child_id"], "call_hash": plan["call_hash"]})
        self.hit("linked_after")

    def receipt(self, task_id: str) -> tuple[dict[str, Any], str] | None:
        event = self.one(self.events(task_id), "receipt_committed")
        if not event:
            return None
        digest = event.get("receipt_hash")
        if not isinstance(digest, str) or not ID_RE.fullmatch(digest):
            raise Corruption("invalid receipt hash")
        path = self.task_dir(task_id) / "payload" / f"{digest}.receipt.json"
        if not path.exists() or path.is_symlink() or sha(path.read_bytes()) != digest:
            raise Corruption("missing or bad receipt payload")
        return self.read_json(path), digest

    def repair(self, task_id: str, reason: str) -> None:
        if not self.one(self.events(task_id), "repair_required"):
            self.append_event(task_id, {"type": "repair_required", "reason": reason})

    def expected_leaf_receipt(self, slot: str, child: str) -> tuple[bytes, str]:
        receipt = {"logical_status": 0, "stderr": {"inline": ""},
                   "stdout": {"inline": f"{slot} complete\\n"}, "task_id": child,
                   "type": "semantic_receipt", "version": 1}
        data = canonical(receipt)
        return data, sha(data)

    def staged_leaf_receipt_is_complete(self, slot: str, child: str) -> tuple[bytes, str] | None:
        # The fake definition makes the one valid semantic result deterministic.
        # This is deliberately not directory scanning or a general receipt ABI.
        data, digest = self.expected_leaf_receipt(slot, child)
        path = self.task_dir(child) / "payload" / f"{digest}.receipt.json"
        if not path.exists():
            return None
        if path.is_symlink() or path.read_bytes() != data or sha(data) != digest:
            raise Corruption("staged fake receipt is not the expected immutable bytes")
        return data, digest

    def run_child(self, slot: str) -> None:
        if not self.linked(slot):
            return
        plan = self.planned(slot)
        assert plan
        self.validate_plan(slot, plan)
        child = plan["child_id"]
        events = self.events(child)
        if self.one(events, "repair_required") or self.receipt(child):
            return
        intent = self.one(events, "dispatch_intent")
        if intent:
            staged = self.staged_leaf_receipt_is_complete(slot, child)
            if staged:
                _, digest = staged
                # Stage complete, semantic event absent: recovery commits only the
                # fake semantic result; it never dispatches the leaf a second time.
                self.hit("child_receipt_commit_before")
                self.append_event(child, {"type": "receipt_committed", "receipt_hash": digest})
                self.hit("child_receipt_commit_after")
                return
            # A committed intent without a trustworthy complete Receipt is unknown.
            self.repair(child, "dispatch_intent_without_trustworthy_receipt")
            self.repair(PARENT_ID, "child_outcome_unknown")
            return
        self.hit("dispatch_intent_before")
        self.append_event(child, {"type": "dispatch_intent", "attempt": 1})
        self.hit("dispatch_intent_after")
        if self.case() == "unknown" and slot == "first":
            # No fake outcome is written.  Recovery turns the durable intent into unknown.
            return
        data, digest = self.expected_leaf_receipt(slot, child)
        payload_dir = self.task_dir(child) / "payload"
        payload_dir.mkdir(exist_ok=True)
        path = payload_dir / f"{digest}.receipt.json"
        if not path.exists():
            self.hit("child_receipt_stage_before")
            self.atomic_bytes(path, data)
            self.hit("child_receipt_stage_after")
        self.hit("child_receipt_commit_before")
        self.append_event(child, {"type": "receipt_committed", "receipt_hash": digest})
        self.hit("child_receipt_commit_after")

    def observe(self, slot: str) -> None:
        if self.observed(slot):
            return
        plan = self.planned(slot)
        if not plan:
            return
        self.validate_plan(slot, plan)
        got = self.receipt(plan["child_id"])
        if not got:
            return
        _, digest = got
        self.hit("parent_observe_before")
        self.append_event(PARENT_ID, {"type": "child_observed", "slot": slot,
                                      "child_id": plan["child_id"], "receipt_hash": digest})
        self.hit("parent_observe_after")

    def parent_receipt(self) -> None:
        if self.receipt(PARENT_ID):
            return
        first, second = self.observed("first"), self.observed("second")
        if not (first and second):
            return
        receipt = {"child_refs": [[first["child_id"], first["receipt_hash"]],
                                  [second["child_id"], second["receipt_hash"]]],
                   "logical_status": 0, "stderr": {"inline": ""},
                   "stdout": {"inline": "sequence complete\\n"}, "task_id": PARENT_ID,
                   "type": "semantic_receipt", "version": 1}
        data, digest = canonical(receipt), sha(canonical(receipt))
        payload_dir = self.task_dir(PARENT_ID) / "payload"
        path = payload_dir / f"{digest}.receipt.json"
        if not path.exists():
            self.hit("parent_receipt_stage_before")
            self.atomic_bytes(path, data)
            self.hit("parent_receipt_stage_after")
        self.hit("parent_receipt_commit_before")
        self.append_event(PARENT_ID, {"type": "receipt_committed", "receipt_hash": digest})
        self.hit("parent_receipt_commit_after")

    def progress(self) -> None:
        # Fixed sequential composite: a later slot cannot exist until prior observation.
        self.publish_plan("first")
        self.materialize("first")
        self.link("first")
        self.run_child("first")
        self.observe("first")
        if self.one(self.parent_events(), "repair_required"):
            return
        if not self.observed("first"):
            return
        self.publish_plan("second")
        self.materialize("second")
        self.link("second")
        self.run_child("second")
        self.observe("second")
        self.parent_receipt()

    def task_state(self, task_id: str) -> str:
        events = self.events(task_id)
        if self.one(events, "repair_required"):
            return "repair_required"
        if self.receipt(task_id):
            return "completed"
        if task_id == PARENT_ID:
            if self.linked("first") or self.linked("second"):
                return "waiting"
            return "running" if events else "queued"
        return "running" if self.one(events, "dispatch_intent") else "queued"

    def report(self, fault: str | None) -> dict[str, Any]:
        errors: list[str] = []
        try:
            parent_events = self.parent_events()
            tree = []
            ids = [PARENT_ID]
            for slot in SLOTS:
                event = self.planned(slot)
                if event:
                    ids.append(event["child_id"])
            for task_id in ids:
                events = self.events(task_id)
                rec = self.receipt(task_id)
                traces = [e["type"] for e in events]
                tree.append({"task_id": task_id, "state": self.task_state(task_id),
                             "state_trace": traces,
                             "dispatch_count": sum(e["type"] == "dispatch_intent" for e in events),
                             "receipt_hash": rec[1] if rec else None})
            # Relation truth comes only from parent events; this catches accidental scan-based logic.
            for slot in SLOTS:
                p, l = self.planned(slot), self.linked(slot)
                if l and not p:
                    errors.append(f"linked without planned: {slot}")
                if p and p["child_id"] != self.child_id(slot):
                    errors.append(f"wrong deterministic id: {slot}")
            result = {"case": self.case(), "phase": "recovered", "fault_point": fault,
                      "task_tree": tree, "parent_events": parent_events,
                      "verification_errors": errors}
        except StoreError as exc:
            result = {"case": self.case(), "phase": "corrupt", "fault_point": fault,
                      "task_tree": [], "verification_errors": [str(exc)]}
        self.atomic_bytes(self.root / "report.json", canonical(result))
        return result


def worker(args: argparse.Namespace) -> int:
    store = Store(Path(args.root), args.fault_point, args.armed)
    store.initialize(args.case)
    store.progress()
    store.report(args.fault_point)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--case", required=True, choices=("success", "unknown"))
    ap.add_argument("--fault-point")
    ap.add_argument("--armed", action="store_true")
    ns = ap.parse_args()
    try:
        return worker(ns)
    except StoreError as exc:
        print(f"store error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
