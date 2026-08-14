#!/usr/bin/env python3
"""P1a-1 v2 model: single-writer, crash/replay fake composite task tree.

This workbench is intentionally small and is not an AOS implementation.  It
uses a fake leaf executor and only claims process-kill/failpoint evidence on a
single Linux filesystem.
"""
from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from p1a_store import (Corruption, ID_RE, StoreError, canonical, exact,
                       fsync_dir, sha, strict_json, task_id, text)

SLOTS = ("first", "second")
ROOT_ID = "a" * 64
CALL_SCHEMA = "aos-p1a-call/v2"
TASK_SCHEMA = "aos-p1a-task/v2"
EVENT_SCHEMA = "aos-p1a-event/v2"
RECEIPT_SCHEMA = "aos-p1a-receipt/v2"


@dataclass(frozen=True)
class TaskView:
    task_id: str
    kind: str
    call_hash: str
    events: tuple[dict[str, Any], ...]
    state: str
    dispatch_count: int
    receipt_hash: str | None
    repair_reason: str | None


@dataclass(frozen=True)
class Projection:
    root: TaskView
    children: dict[str, TaskView]
    planned: dict[str, dict[str, Any]]
    linked: dict[str, dict[str, Any]]
    observed: dict[str, dict[str, Any]]
    phase: str
    repairable_tails: tuple[tuple[str, int], ...]


class Store:
    def __init__(self, raw_root: str, case: str, fail: str | None = None, armed: bool = False):
        # Do not resolve: resolving first would hide a symlink supplied as root.
        if not os.path.isabs(raw_root):
            raise StoreError("root must be lexical absolute path")
        self.root = Path(raw_root)
        self.case_name = case
        self.fail = fail
        self.armed = armed

    def validate_root_ancestors(self) -> None:
        """Reject every *existing* lexical ancestor symlink before any mkdir.

        This is deliberately not an openat/dirfd security construction; an
        attacker can still race a later path operation.  It only keeps the
        workbench from silently resolving an already-present symlink.
        """
        chain = list(self.root.parents)[::-1] + [self.root]
        for part in chain:
            try:
                os.lstat(part)
            except FileNotFoundError:
                continue
            except OSError as exc:
                raise Corruption("cannot lstat root ancestor: " + str(part)) from exc
            if os.path.islink(part):
                raise Corruption("symlink rejected in root ancestor: " + str(part))

    def lstat_kind(self, path: Path, wanted: str, required: bool = True) -> bool:
        try:
            st = os.lstat(path)
        except FileNotFoundError:
            if required:
                raise Corruption("missing " + str(path))
            return False
        if os.path.islink(path):
            raise Corruption("symlink rejected: " + str(path))
        if wanted == "dir" and not os.path.isdir(path):
            raise Corruption("directory expected: " + str(path))
        if wanted == "file" and not os.path.isfile(path):
            raise Corruption("regular file expected: " + str(path))
        return True

    def root_components(self, create: bool = False) -> tuple[Path, Path]:
        self.validate_root_ancestors()
        if create:
            self.root.mkdir(parents=True, exist_ok=True)
        self.lstat_kind(self.root, "dir")
        tasks = self.root / "tasks"
        if create:
            tasks.mkdir(exist_ok=True)
        self.lstat_kind(tasks, "dir")
        return self.root, tasks

    def task_dir(self, ident: str, create: bool = False) -> Path:
        ident = task_id(ident)
        _, tasks = self.root_components(create=create)
        path = tasks / ident
        if create:
            path.mkdir(exist_ok=True)
            fsync_dir(tasks)
        self.lstat_kind(path, "dir")
        return path

    def file(self, path: Path, required: bool = True) -> bool:
        return self.lstat_kind(path, "file", required)

    def atomic(self, target: Path, data: bytes) -> None:
        self.lstat_kind(target.parent, "dir")
        if self.lstat_kind(target, "file", required=False):
            old = target.read_bytes()
            if old != data:
                raise Corruption("immutable file differs: " + str(target))
            return
        temp = target.parent / ("." + target.name + ".tmp-" + str(os.getpid()))
        fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            pos = 0
            while pos < len(data):
                pos += os.write(fd, data[pos:])
            os.fsync(fd)
        finally:
            os.close(fd)
        os.replace(temp, target)
        fsync_dir(target.parent)

    def replace_report(self, target: Path, data: bytes) -> None:
        """`report.json` is a derived mutable projection, unlike Call/Receipt."""
        self.lstat_kind(target.parent, "dir")
        if target.exists():
            self.file(target)
        temp = target.parent / ("." + target.name + ".tmp-" + str(os.getpid()))
        fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            pos = 0
            while pos < len(data):
                pos += os.write(fd, data[pos:])
            os.fsync(fd)
        finally:
            os.close(fd)
        os.replace(temp, target)
        fsync_dir(target.parent)

    def read_obj(self, path: Path, label: str) -> dict[str, Any]:
        self.file(path)
        obj = strict_json(path.read_bytes(), label)
        if not isinstance(obj, dict):
            raise Corruption(label + " must be object")
        return obj

    def append(self, ident: str, event: dict[str, Any]) -> None:
        path = self.task_dir(ident) / "events.jsonl"
        if path.exists():
            self.file(path)
        data = canonical(event) + b"\n"
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        try:
            pos = 0
            while pos < len(data):
                wrote = os.write(fd, data[pos:])
                if wrote <= 0:
                    raise OSError("short event write")
                pos += wrote
            os.fsync(fd)
        finally:
            os.close(fd)
        fsync_dir(path.parent)

    def read_events(self, ident: str) -> tuple[dict[str, Any], ...]:
        path = self.task_dir(ident) / "events.jsonl"
        if not path.exists():
            return ()
        self.file(path)
        data = path.read_bytes()
        if not data:
            return ()
        lines = data.split(b"\n")
        committed = lines[:-1] if data.endswith(b"\n") else lines[:-1]
        tail = b"" if data.endswith(b"\n") else lines[-1]
        out: list[dict[str, Any]] = []
        for index, line in enumerate(committed):
            if not line:
                raise Corruption(f"empty committed event {ident}:{index}")
            obj = strict_json(line, f"event {ident}:{index}")
            if not isinstance(obj, dict):
                raise Corruption("event must be object")
            out.append(obj)
        if tail:
            # Never mutate while validating/reporting.  `validate_store` starts a
            # fresh collection and publishes this exact fixed path/prefix offset
            # only after every schema, relation and Receipt check succeeds.
            self._repairable_tails[ident] = len(data) - len(tail)
        return tuple(out)

    def truncate_verified_tail(self, ident: str, prefix_len: int) -> None:
        """Recovery-only mutation; caller got ident/offset from a valid Projection."""
        path = self.task_dir(ident) / "events.jsonl"
        self.file(path)  # lstat regular/non-symlink before open: never block FIFO.
        data = path.read_bytes()
        if prefix_len < 0 or prefix_len > len(data) or data[:prefix_len].count(b"\n") == 0 and prefix_len:
            raise Corruption("invalid verified torn-tail offset")
        fd = os.open(path, os.O_WRONLY)
        try:
            os.ftruncate(fd, prefix_len)
            os.fsync(fd)
        finally:
            os.close(fd)
        fsync_dir(path.parent)

    def hit(self, point: str) -> None:
        marker = self.root / ".failpoint-used"
        if self.armed and self.fail == point and not marker.exists():
            self.atomic(marker, point.encode("ascii"))
            os._exit(97)

    @staticmethod
    def leaf_call() -> dict[str, Any]:
        # Deliberately identical bytes for first and second: task/relation/outcome
        # are not part of a Call.
        return {"schema": CALL_SCHEMA, "definition": "/fixture/fake-leaf",
                "definition_generation": "fake-leaf-v2", "argv": ["fake-leaf"],
                "stdin": {"kind": "utf8", "data": "p1a fixture\n"},
                "cwd": "/tmp", "env": {}, "fixture_policy": "leaf-success"}

    @staticmethod
    def root_call() -> dict[str, Any]:
        return {"schema": CALL_SCHEMA, "definition": "/fixture/sequence-two",
                "definition_generation": "sequence-two-v2", "argv": ["sequence-two"],
                "stdin": {"kind": "utf8", "data": ""}, "cwd": "/tmp", "env": {},
                "fixture_policy": "ordered-first-second"}

    def call_bytes(self, kind: str) -> tuple[bytes, str]:
        obj = self.root_call() if kind == "composite" else self.leaf_call()
        self.validate_call(obj)
        data = canonical(obj)
        return data, sha(data)

    def validate_call(self, obj: dict[str, Any]) -> None:
        exact(obj, {"schema", "definition", "definition_generation", "argv", "stdin", "cwd", "env", "fixture_policy"}, "call")
        if obj["schema"] != CALL_SCHEMA:
            raise Corruption("wrong call schema")
        if not os.path.isabs(text(obj["definition"], "definition")) or not os.path.isabs(text(obj["cwd"], "cwd")):
            raise Corruption("call definition/cwd must be lexical absolute")
        text(obj["definition_generation"], "definition_generation")
        if not isinstance(obj["argv"], list) or not all(isinstance(x, str) for x in obj["argv"]):
            raise Corruption("call argv invalid")
        if not isinstance(obj["env"], dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in obj["env"].items()):
            raise Corruption("call env invalid")
        stdin = obj["stdin"]
        if not isinstance(stdin, dict) or set(stdin) != {"kind", "data"} or stdin.get("kind") != "utf8" or not isinstance(stdin.get("data"), str):
            raise Corruption("call stdin invalid")
        text(obj["fixture_policy"], "fixture_policy")

    def initialize(self) -> None:
        if self.case_name not in ("success", "unknown"):
            raise StoreError("case must be success or unknown")
        self.root_components(create=True)
        fixture = self.root / "fixture.json"
        expected = canonical({"schema": "aos-p1a-fixture/v2", "case": self.case_name})
        self.atomic(fixture, expected)
        root = self.task_dir(ROOT_ID, create=True)
        payload = root / "payload"
        payload.mkdir(exist_ok=True)
        self.lstat_kind(payload, "dir")
        call_data, call_hash = self.call_bytes("composite")
        self.atomic(root / "call.json", call_data)
        self.atomic(root / "task.json", canonical({"schema": TASK_SCHEMA, "task_id": ROOT_ID,
                                                     "call_ref": {"hash": call_hash, "size": len(call_data)}}))
        if not (root / "events.jsonl").exists():
            self.append(ROOT_ID, {"schema": EVENT_SCHEMA, "type": "accepted"})

    def validate_task_base(self, ident: str, kind: str) -> tuple[str, int]:
        folder = self.task_dir(ident)
        payload = folder / "payload"
        self.lstat_kind(payload, "dir")
        task = self.read_obj(folder / "task.json", "task.json")
        exact(task, {"schema", "task_id", "call_ref"}, "task.json")
        if task["schema"] != TASK_SCHEMA or task_id(task["task_id"]) != ident:
            raise Corruption("bad task identity")
        call_ref = task["call_ref"]
        if not isinstance(call_ref, dict):
            raise Corruption("bad call_ref")
        exact(call_ref, {"hash", "size"}, "call_ref")
        digest = task_id(call_ref["hash"], "call hash")
        if not isinstance(call_ref["size"], int) or call_ref["size"] < 0:
            raise Corruption("call size invalid")
        call_path = folder / "call.json"
        self.file(call_path)
        data = call_path.read_bytes()
        if sha(data) != digest or len(data) != call_ref["size"]:
            raise Corruption("call_ref mismatch")
        call = strict_json(data, "call.json")
        if not isinstance(call, dict):
            raise Corruption("call must be object")
        self.validate_call(call)
        expected = self.call_bytes("composite" if kind == "composite" else "leaf")
        if data != expected[0]:
            raise Corruption("fixture Call bytes differ")
        return digest, len(data)

    def validate_event_shape(self, event: dict[str, Any], owner: str) -> None:
        if event.get("schema") != EVENT_SCHEMA or not isinstance(event.get("type"), str):
            raise Corruption("event schema/type invalid")
        typ = event["type"]
        if typ == "accepted":
            exact(event, {"schema", "type"}, "accepted")
        elif typ == "dispatch_intent":
            exact(event, {"schema", "type", "attempt_id"}, "dispatch_intent")
            if event["attempt_id"] != "attempt-1":
                raise Corruption("only attempt-1 exists in P1a")
        elif typ == "receipt_committed":
            exact(event, {"schema", "type", "receipt_hash"}, "receipt_committed")
            task_id(event["receipt_hash"], "receipt hash")
        elif typ == "repair_required":
            exact(event, {"schema", "type", "reason"}, "repair_required")
            text(event["reason"], "repair reason")
        elif owner == "parent" and typ == "child_planned":
            exact(event, {"schema", "type", "slot", "task_id", "call_hash"}, "child_planned")
            if event["slot"] not in SLOTS:
                raise Corruption("bad slot")
            task_id(event["task_id"]); task_id(event["call_hash"], "call hash")
        elif owner == "parent" and typ == "child_linked":
            exact(event, {"schema", "type", "slot", "task_id", "call_hash"}, "child_linked")
            if event["slot"] not in SLOTS:
                raise Corruption("bad slot")
            task_id(event["task_id"]); task_id(event["call_hash"], "call hash")
        elif owner == "parent" and typ == "child_observed":
            exact(event, {"schema", "type", "slot", "task_id", "receipt_hash"}, "child_observed")
            if event["slot"] not in SLOTS:
                raise Corruption("bad slot")
            task_id(event["task_id"]); task_id(event["receipt_hash"], "receipt hash")
        else:
            raise Corruption("unknown event type for " + owner + ": " + typ)

    def receipt(self, ident: str, call_hash: str, kind: str, events: tuple[dict[str, Any], ...]) -> tuple[dict[str, Any], str] | None:
        commits = [e for e in events if e["type"] == "receipt_committed"]
        if not commits:
            return None
        if len(commits) != 1:
            raise Corruption("duplicate receipt commit")
        digest = task_id(commits[0]["receipt_hash"], "receipt hash")
        path = self.task_dir(ident) / "payload" / (digest + ".receipt.json")
        self.file(path)
        data = path.read_bytes()
        if sha(data) != digest:
            raise Corruption("receipt hash mismatch")
        obj = strict_json(data, "receipt")
        if not isinstance(obj, dict):
            raise Corruption("receipt must object")
        self.validate_receipt(obj, ident, call_hash, kind)
        return obj, digest

    def validate_return(self, value: Any, expected_stdout: str) -> None:
        if not isinstance(value, dict):
            raise Corruption("return must object")
        exact(value, {"logical_status", "stdout", "stderr"}, "return")
        if value["logical_status"] != 0:
            raise Corruption("P1a return status must be zero")
        for key, expected in (("stdout", expected_stdout), ("stderr", "")):
            stream = value[key]
            if not isinstance(stream, dict) or set(stream) != {"kind", "data"} or stream.get("kind") != "utf8" or stream.get("data") != expected:
                raise Corruption("bad return " + key)

    def validate_receipt(self, obj: dict[str, Any], ident: str, call_hash: str, kind: str) -> None:
        exact(obj, {"schema", "task_id", "call_hash", "basis", "return"}, "receipt")
        if obj["schema"] != RECEIPT_SCHEMA or task_id(obj["task_id"]) != ident or task_id(obj["call_hash"], "receipt call hash") != call_hash:
            raise Corruption("receipt identity/call mismatch")
        basis = obj["basis"]
        if kind == "leaf":
            if not isinstance(basis, dict) or basis != {"kind": "attempt", "attempt_id": "attempt-1"}:
                raise Corruption("leaf receipt basis invalid")
            slot = "first" if ident == child_id(ROOT_ID, "first") else "second"
            self.validate_return(obj["return"], slot + " complete\n")
        else:
            if not isinstance(basis, dict):
                raise Corruption("parent receipt basis invalid")
            exact(basis, {"kind", "items"}, "parent receipt basis")
            if basis["kind"] != "children" or not isinstance(basis["items"], list) or len(basis["items"]) != 2:
                raise Corruption("parent receipt child refs invalid")
            self.validate_return(obj["return"], "sequence complete\n")

    def validate_leaf(self, ident: str, call_hash: str) -> TaskView:
        events = self.read_events(ident)
        if not events:
            # A staged child directory/call alone is an orphan and has no Task.
            raise Corruption("linked child lacks accepted event")
        for event in events:
            self.validate_event_shape(event, "child")
        if events[0]["type"] != "accepted":
            raise Corruption("child must begin accepted")
        types = [e["type"] for e in events]
        if types.count("accepted") != 1 or types.count("dispatch_intent") > 1 or types.count("receipt_committed") > 1 or types.count("repair_required") > 1:
            raise Corruption("child duplicate event")
        terminal = next((i for i, x in enumerate(types) if x in ("receipt_committed", "repair_required")), None)
        if terminal is not None and terminal != len(events) - 1:
            raise Corruption("child event after terminal")
        if "dispatch_intent" in types and types.index("dispatch_intent") != 1:
            raise Corruption("child intent ordering")
        if "receipt_committed" in types:
            if types != ["accepted", "dispatch_intent", "receipt_committed"]:
                raise Corruption("child receipt ordering")
        if "repair_required" in types:
            if types != ["accepted", "dispatch_intent", "repair_required"]:
                raise Corruption("child repair ordering")
        got = self.receipt(ident, call_hash, "leaf", events)
        if "receipt_committed" in types and not got:
            raise Corruption("receipt event without receipt")
        state = "completed" if got else "repair_required" if "repair_required" in types else "running" if "dispatch_intent" in types else "queued"
        return TaskView(ident, "leaf", call_hash, events, state, types.count("dispatch_intent"), got[1] if got else None,
                        next((e["reason"] for e in events if e["type"] == "repair_required"), None))

    def validate_store(self) -> Projection:
        self._repairable_tails: dict[str, int] = {}
        self.root_components()
        fixture = self.read_obj(self.root / "fixture.json", "fixture")
        if fixture != {"schema": "aos-p1a-fixture/v2", "case": self.case_name}:
            raise Corruption("fixture differs")
        root_hash, _ = self.validate_task_base(ROOT_ID, "composite")
        parent_events = self.read_events(ROOT_ID)
        for event in parent_events:
            self.validate_event_shape(event, "parent")
        if not parent_events or parent_events[0]["type"] != "accepted" or sum(e["type"] == "accepted" for e in parent_events) != 1:
            raise Corruption("parent must begin once with accepted")
        planned: dict[str, dict[str, Any]] = {}
        linked: dict[str, dict[str, Any]] = {}
        observed: dict[str, dict[str, Any]] = {}
        children: dict[str, TaskView] = {}
        terminal = False
        expected_slot_index = 0
        for event in parent_events[1:]:
            typ = event["type"]
            if terminal:
                raise Corruption("parent event after terminal")
            if typ == "child_planned":
                slot = event["slot"]
                if slot in planned or expected_slot_index >= len(SLOTS) or slot != SLOTS[expected_slot_index]:
                    raise Corruption("parent planned ordering/duplicate")
                expected = child_id(ROOT_ID, slot)
                if event["task_id"] != expected:
                    raise Corruption("planned child id differs")
                # The locally-derived child call is the only legal payload location.
                folder = self.task_dir(expected)
                if (folder / "task.json").exists():
                    child_hash, _ = self.validate_task_base(expected, "leaf")
                else:
                    child_hash, _ = self.stage_call_view(expected)
                if event["call_hash"] != child_hash:
                    raise Corruption("planned call hash differs")
                planned[slot] = event
            elif typ == "child_linked":
                slot = event["slot"]
                if slot not in planned or slot in linked or event["task_id"] != planned[slot]["task_id"] or event["call_hash"] != planned[slot]["call_hash"]:
                    raise Corruption("linked does not match planned")
                child_hash, _ = self.validate_task_base(event["task_id"], "leaf")
                if child_hash != event["call_hash"]:
                    raise Corruption("linked child Call differs")
                child = self.validate_leaf(event["task_id"], child_hash)
                children[slot] = child
                linked[slot] = event
            elif typ == "child_observed":
                slot = event["slot"]
                if slot not in linked or slot in observed or event["task_id"] != linked[slot]["task_id"]:
                    raise Corruption("observed does not match linked")
                if SLOTS.index(slot) > 0 and SLOTS[SLOTS.index(slot)-1] not in observed:
                    raise Corruption("observed before previous slot")
                child = children[slot]
                if child.receipt_hash != event["receipt_hash"]:
                    raise Corruption("observed receipt differs")
                observed[slot] = event
                expected_slot_index += 1
            elif typ == "repair_required":
                # Parent repair is only legal after a linked child visibly needs repair.
                if not any(c.state == "repair_required" for c in children.values()):
                    raise Corruption("parent repair lacks child repair")
                terminal = True
            elif typ == "receipt_committed":
                if set(observed) != set(SLOTS):
                    raise Corruption("parent receipt before all observations")
                terminal = True
            else:
                raise Corruption("parent may not contain " + typ)
        # Validate pre-link children only after the complete parent relation stream
        # is known: a completed child naturally has later events than its earlier
        # `child_planned` record.
        for slot, plan in planned.items():
            if slot in linked:
                continue
            folder = self.task_dir(plan["task_id"])
            if (folder / "task.json").exists():
                early = self.read_events(plan["task_id"])
                if early and (len(early) != 1 or early[0] != {"schema": EVENT_SCHEMA, "type": "accepted"}):
                    raise Corruption("unlinked child has non-accepted event")
        parent_receipt = self.receipt(ROOT_ID, root_hash, "composite", parent_events)
        if any(e["type"] == "receipt_committed" for e in parent_events):
            if not parent_receipt:
                raise Corruption("parent receipt missing")
            basis = parent_receipt[0]["basis"]["items"]
            expected_basis = [{"slot": slot, "task_id": observed[slot]["task_id"], "receipt_hash": observed[slot]["receipt_hash"]} for slot in SLOTS]
            if basis != expected_basis:
                raise Corruption("parent receipt child refs/order differ")
        root_state = "completed" if parent_receipt else "repair_required" if terminal else "waiting" if linked else "running"
        root_view = TaskView(ROOT_ID, "composite", root_hash, parent_events, root_state, 0,
                             parent_receipt[1] if parent_receipt else None,
                             next((e["reason"] for e in parent_events if e["type"] == "repair_required"), None))
        return Projection(root_view, children, planned, linked, observed, "recovery",
                          tuple(sorted(self._repairable_tails.items())))

    def stage_call_view(self, ident: str) -> tuple[str, int]:
        # An unlinked staging directory may contain call.json but must not count as a Task.
        folder = self.task_dir(ident)
        self.lstat_kind(folder / "payload", "dir")
        if (folder / "task.json").exists() or (folder / "events.jsonl").exists():
            raise Corruption("child Task materialized without task.json/events")
        self.file(folder / "call.json")
        data = (folder / "call.json").read_bytes()
        obj = strict_json(data, "staged child call")
        if not isinstance(obj, dict):
            raise Corruption("staged call object expected")
        self.validate_call(obj)
        expected, digest = self.call_bytes("leaf")
        if data != expected:
            raise Corruption("staged Call bytes differ")
        return digest, len(data)

    def child_stage(self, slot: str) -> None:
        ident = child_id(ROOT_ID, slot)
        folder = self.task_dir(ident, create=True)
        payload = folder / "payload"
        payload.mkdir(exist_ok=True)
        self.lstat_kind(payload, "dir")
        data, _ = self.call_bytes("leaf")
        self.hit("call_payload_before")
        self.atomic(folder / "call.json", data)
        self.hit("call_payload_after")

    def add_plan(self, slot: str) -> None:
        data, digest = self.call_bytes("leaf")
        del data
        self.hit("planned_before")
        self.append(ROOT_ID, {"schema": EVENT_SCHEMA, "type": "child_planned", "slot": slot,
                              "task_id": child_id(ROOT_ID, slot), "call_hash": digest})
        self.hit("planned_after")

    def materialize(self, slot: str) -> None:
        ident = child_id(ROOT_ID, slot)
        folder = self.task_dir(ident)
        data, digest = self.call_bytes("leaf")
        self.hit("materialize_before")
        self.atomic(folder / "task.json", canonical({"schema": TASK_SCHEMA, "task_id": ident,
                                                       "call_ref": {"hash": digest, "size": len(data)}}))
        self.hit("materialize_task_after")
        self.hit("accepted_before")
        self.append(ident, {"schema": EVENT_SCHEMA, "type": "accepted"})
        self.hit("accepted_after")
        self.hit("materialize_after")

    def add_link(self, slot: str, plan: dict[str, Any]) -> None:
        self.hit("linked_before")
        self.append(ROOT_ID, {"schema": EVENT_SCHEMA, "type": "child_linked", "slot": slot,
                              "task_id": plan["task_id"], "call_hash": plan["call_hash"]})
        self.hit("linked_after")

    def leaf_receipt_bytes(self, slot: str, ident: str, call_hash: str) -> tuple[bytes, str]:
        obj = {"schema": RECEIPT_SCHEMA, "task_id": ident, "call_hash": call_hash,
               "basis": {"kind": "attempt", "attempt_id": "attempt-1"},
               "return": {"logical_status": 0, "stdout": {"kind": "utf8", "data": slot + " complete\n"},
                          "stderr": {"kind": "utf8", "data": ""}}}
        data = canonical(obj)
        return data, sha(data)

    def parent_receipt_bytes(self, projection: Projection) -> tuple[bytes, str]:
        items = [{"slot": slot, "task_id": projection.observed[slot]["task_id"],
                  "receipt_hash": projection.observed[slot]["receipt_hash"]} for slot in SLOTS]
        obj = {"schema": RECEIPT_SCHEMA, "task_id": ROOT_ID, "call_hash": projection.root.call_hash,
               "basis": {"kind": "children", "items": items},
               "return": {"logical_status": 0, "stdout": {"kind": "utf8", "data": "sequence complete\n"},
                          "stderr": {"kind": "utf8", "data": ""}}}
        data = canonical(obj)
        return data, sha(data)

    def progress_once(self) -> bool:
        p = self.validate_store()
        if p.repairable_tails:
            for ident, prefix_len in p.repairable_tails:
                self.truncate_verified_tail(ident, prefix_len)
            p = self.validate_store()
        # A report after the very first normal run is run, later invocations recovery.
        if p.root.state in ("completed", "repair_required"):
            return False
        for slot in SLOTS:
            if slot not in p.planned:
                if slot == "second" and "first" not in p.observed:
                    return False
                self.child_stage(slot); self.add_plan(slot); self.validate_store(); return True
            plan = p.planned[slot]
            ident = plan["task_id"]
            if slot not in p.linked:
                if not (self.task_dir(ident) / "task.json").exists():
                    self.materialize(slot); self.validate_store(); return True
                if not self.read_events(ident):
                    self.hit("accepted_before")
                    self.append(ident, {"schema": EVENT_SCHEMA, "type": "accepted"})
                    self.hit("accepted_after")
                    self.validate_store(); return True
                self.add_link(slot, plan); self.validate_store(); return True
            child = p.children[slot]
            if child.state == "repair_required":
                self.append(ROOT_ID, {"schema": EVENT_SCHEMA, "type": "repair_required", "reason": "child_outcome_unknown"})
                self.validate_store(); return True
            if child.receipt_hash is None:
                if child.dispatch_count == 0:
                    self.hit("dispatch_intent_before")
                    self.append(ident, {"schema": EVENT_SCHEMA, "type": "dispatch_intent", "attempt_id": "attempt-1"})
                    self.hit("dispatch_intent_after")
                    if self.case_name == "unknown" and slot == "first":
                        self.validate_store(); return True
                    data, digest = self.leaf_receipt_bytes(slot, ident, child.call_hash)
                    path = self.task_dir(ident) / "payload" / (digest + ".receipt.json")
                    self.hit("child_receipt_stage_before")
                    self.atomic(path, data)
                    self.hit("child_receipt_stage_after")
                    self.hit("child_receipt_commit_before")
                    self.append(ident, {"schema": EVENT_SCHEMA, "type": "receipt_committed", "receipt_hash": digest})
                    self.hit("child_receipt_commit_after")
                    self.validate_store(); return True
                # A new invocation only sees durable intent.  A valid staged Receipt
                # can be committed; otherwise it must stop as unknown.
                data, digest = self.leaf_receipt_bytes(slot, ident, child.call_hash)
                path = self.task_dir(ident) / "payload" / (digest + ".receipt.json")
                if path.exists():
                    self.file(path)  # reject FIFO/socket/device/directory before read.
                if path.exists() and path.read_bytes() == data and sha(data) == digest:
                    self.hit("child_receipt_commit_before")
                    self.append(ident, {"schema": EVENT_SCHEMA, "type": "receipt_committed", "receipt_hash": digest})
                    self.hit("child_receipt_commit_after")
                else:
                    self.append(ident, {"schema": EVENT_SCHEMA, "type": "repair_required", "reason": "intent_without_complete_receipt"})
                self.validate_store(); return True
            if slot not in p.observed:
                self.hit("parent_observe_before")
                self.append(ROOT_ID, {"schema": EVENT_SCHEMA, "type": "child_observed", "slot": slot,
                                      "task_id": ident, "receipt_hash": child.receipt_hash})
                self.hit("parent_observe_after")
                self.validate_store(); return True
        data, digest = self.parent_receipt_bytes(p)
        path = self.task_dir(ROOT_ID) / "payload" / (digest + ".receipt.json")
        self.hit("parent_receipt_stage_before")
        self.atomic(path, data)
        self.hit("parent_receipt_stage_after")
        self.hit("parent_receipt_commit_before")
        self.append(ROOT_ID, {"schema": EVENT_SCHEMA, "type": "receipt_committed", "receipt_hash": digest})
        self.hit("parent_receipt_commit_after")
        self.validate_store(); return True

    def run(self) -> Projection:
        phase = "run"
        for _ in range(64):
            if not self.progress_once():
                p = self.validate_store()
                return Projection(p.root, p.children, p.planned, p.linked, p.observed, phase,
                                  p.repairable_tails)
            phase = "run"
        raise StoreError("progress did not converge")

    def report(self, fault: str | None, phase: str) -> dict[str, Any]:
        p = self.validate_store()
        def item(view: TaskView, slot: str | None = None) -> dict[str, Any]:
            call = self.root_call() if view.kind == "composite" else self.leaf_call()
            return {"task_id": view.task_id, "definition": call["definition"],
                    "argv_summary": " ".join(call["argv"]),
                    "slot": slot, "state": view.state, "dispatch_count": view.dispatch_count,
                    "receipt_hash": view.receipt_hash, "reason": view.repair_reason,
                    "blocked_on": None if view.state != "waiting" else "child receipt",
                    "return_status": 0 if view.receipt_hash else None}
        children = [item(p.children[s], s) for s in SLOTS if s in p.children]
        return {"case": self.case_name, "phase": phase, "fault_point": fault,
                "task_tree": {"root": item(p.root), "children": children}, "verification_errors": []}


def child_id(parent: str, slot: str) -> str:
    if slot not in SLOTS:
        raise Corruption("unknown slot")
    return sha(b"aos-p1a-child/v2\0" + parent.encode("ascii") + b"\0" + slot.encode("ascii"))


def write_report(store: Store, fault: str | None, phase: str) -> None:
    try:
        obj = store.report(fault, phase)
    except (StoreError, OSError) as exc:
        obj = {"case": store.case_name, "phase": "corrupt", "fault_point": fault,
               "task_tree": None, "verification_errors": [str(exc)]}
    try:
        store.replace_report(store.root / "report.json", canonical(obj))
    except (Corruption, OSError):
        # If the supplied root itself is a symlink, writing beneath it would turn a
        # rejection into a write through the attacker-controlled link.  Preserve a
        # fail-closed diagnostic beside the lexical root instead.
        sidecar = Path(str(store.root) + ".corrupt-report.json")
        store.replace_report(sidecar, canonical(obj))
        return
    # A previous root-symlink rejection may have left a safe sidecar beside a
    # later healthy lexical root.  Remove only this exact regular/symlink leaf.
    sidecar = Path(str(store.root) + ".corrupt-report.json")
    if obj["phase"] != "corrupt" and os.path.lexists(sidecar):
        os.unlink(sidecar)
        fsync_dir(sidecar.parent)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--case", choices=("success", "unknown"), required=True)
    ap.add_argument("--fault-point")
    ap.add_argument("--armed", action="store_true")
    ap.add_argument("--report-only", action="store_true", help="validate/report without recovery mutation")
    ns = ap.parse_args()
    try:
        store = Store(ns.root, ns.case, ns.fault_point, ns.armed)
        fresh = not Path(ns.root).exists()
        if ns.report_only:
            if fresh:
                raise StoreError("report-only needs an existing store")
        else:
            store.initialize()
            store.run()
        write_report(store, ns.fault_point, "run" if fresh else "recovery")
        return 0
    except (StoreError, OSError) as exc:
        try:
            write_report(Store(ns.root, ns.case, ns.fault_point, False), ns.fault_point, "corrupt")
        except Exception:
            pass
        print("store error: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
