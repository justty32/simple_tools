"""Exact formats for the P1a-2A sequence_two_fake workbench slice."""
from __future__ import annotations

import os
import stat
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from p1a2_model import (
    CALL, EVENTS, ROOTS, ROOT_FAILPOINTS, TASK, Runtime as RootRuntime,
    _bytes_result, _root_event, _task_bytes, _validate_task,
)
from p1a2_store import (
    AcceptanceRejected, Corruption, LogRead, MAX_DIRECTORY_ENTRIES, MAX_JSON,
    StoreError, StorePaths, TornTail, append_json_line, atomic_publish,
    bounded_tree_snapshot, canonical_json, directory_chain_snapshot,
    ensure_canonical_size, exact_fields, fsync_directory, integer, is_temp_for,
    list_directory, make_ref, read_json_log, read_regular, require_directory,
    root_id, strict_json, task_id, truncate_verified_tail,
)

SLOTS = ("first", "second")
PAYLOAD = "payload"
COMPOSITE_CUTS = tuple(
    f"{slot}_{stage}_after" for slot in SLOTS for stage in (
        "task_dir", "call", "parent_planned", "task", "accepted",
        "parent_linked", "payload_dir", "receipt_payload",
        "receipt_committed", "parent_observed",
    )
) + ("root_payload_dir_after", "root_receipt_payload_after",
     "root_receipt_committed_after")
ALL_FAKE_FAILPOINTS = ROOT_FAILPOINTS + COMPOSITE_CUTS


class CapacityUnavailable(StoreError):
    pass


class StagingCollision(StoreError):
    pass


def derive_child_id(root: str, slot: str) -> str:
    root = root_id(root)
    if slot not in SLOTS:
        raise Corruption("child slot must be first or second")
    return task_id(root + "--" + slot)


def validate_fake_result(value: Any, label: str) -> dict[str, Any]:
    value = exact_fields(value, {"termination", "stdout", "stderr"}, label)
    _bytes_result(value, label)
    return value


def validate_root_fake_call(value: Any) -> dict[str, Any]:
    ensure_canonical_size(value, MAX_JSON, "fake root Call")
    value = exact_fields(value, {"v", "kind", "children", "first_success_oracle"},
                         "fake root Call")
    if integer(value["v"], "fake root Call.v", 1, 1) != 1 or value["kind"] != "sequence_two_fake":
        raise Corruption("fake root Call: v/kind mismatch")
    children = value["children"]
    if not isinstance(children, list) or len(children) != 2:
        raise Corruption("fake root Call: exactly two children required")
    for index, slot in enumerate(SLOTS):
        wrapper = exact_fields(children[index], {"slot", "recipe"}, slot + " child")
        if wrapper["slot"] != slot:
            raise Corruption("fake root Call: child order mismatch")
        recipe = exact_fields(wrapper["recipe"], {"kind", "result"}, slot + " recipe")
        if recipe["kind"] != "fake":
            raise Corruption(slot + " recipe must be fake")
        validate_fake_result(recipe["result"], slot + " result")
    oracle = validate_fake_result(value["first_success_oracle"], "first success oracle")
    termination, _, stderr = _bytes_result(oracle, "first success oracle")
    if termination != {"kind": "exited", "code": 0} or stderr != b"":
        raise Corruption("first success oracle must be exited(0) with empty stderr")
    encoded = canonical_json(value)
    if len(encoded) > MAX_JSON:
        raise Corruption("fake root Call exceeds 1 MiB")
    return value


def root_fake_call_bytes(value: Any) -> bytes:
    return canonical_json(validate_root_fake_call(value))


def fake_call_value(root_call: dict[str, Any], slot: str) -> dict[str, Any]:
    root_call = validate_root_fake_call(root_call)
    if slot not in SLOTS:
        raise Corruption("child slot must be first or second")
    recipe = root_call["children"][SLOTS.index(slot)]["recipe"]
    return {"v": 1, "kind": "fake", "result": recipe["result"]}


def validate_fake_call(value: Any) -> dict[str, Any]:
    ensure_canonical_size(value, MAX_JSON, "fake Call")
    value = exact_fields(value, {"v", "kind", "result"}, "fake Call")
    if integer(value["v"], "fake Call.v", 1, 1) != 1 or value["kind"] != "fake":
        raise Corruption("fake Call: v/kind mismatch")
    validate_fake_result(value["result"], "fake Call result")
    return value


def fake_call_bytes(root_call: dict[str, Any], slot: str) -> bytes:
    return canonical_json(validate_fake_call(fake_call_value(root_call, slot)))


def fake_leaf_receipt_bytes(task: str, call_ref: dict[str, Any], slot: str,
                            result: dict[str, Any]) -> bytes:
    task = task_id(task)
    if slot not in SLOTS:
        raise Corruption("fake Receipt slot must be first or second")
    termination, stdout, stderr = _bytes_result(
        validate_fake_result(result, "fake Receipt result"), "fake Receipt result")
    value = {
        "v": 1, "kind": "leaf", "task_id": task, "call_ref": call_ref,
        "basis": {"kind": "fixture_fake", "slot": slot},
        "return": {"termination": termination, "stdout_ref": make_ref(stdout),
                   "stderr_ref": make_ref(stderr)},
    }
    ensure_canonical_size(value, MAX_JSON, "fake Receipt")
    return canonical_json(value)


def composite_receipt_bytes(root: str, call_ref: dict[str, Any],
                            first_ref: dict[str, Any], second_ref: dict[str, Any]) -> bytes:
    root = root_id(root)
    value = {
        "v": 1, "kind": "composite", "task_id": root, "call_ref": call_ref,
        "basis": {"children": [
            {"slot": "first", "task_id": derive_child_id(root, "first"),
             "receipt_ref": first_ref},
            {"slot": "second", "task_id": derive_child_id(root, "second"),
             "receipt_ref": second_ref},
        ]},
        "return": {"kind": "fixture_sequence_two_success"},
    }
    ensure_canonical_size(value, MAX_JSON, "composite Receipt")
    return canonical_json(value)


def receipt_path(folder: Path, receipt: bytes) -> Path:
    return folder / "payload" / (make_ref(receipt)["sha256"] + ".receipt.json")


def _authority_call(data: bytes, label: str) -> dict[str, Any]:
    value = strict_json(data, label)
    if canonical_json(value) != data:
        raise Corruption(label + ": non-canonical bytes")
    return validate_root_fake_call(value)


def _same_event(actual: dict[str, Any], expected: dict[str, Any]) -> bool:
    return canonical_json(actual) == canonical_json(expected)


def _is_prefix(actual: tuple[dict[str, Any], ...],
               expected: list[dict[str, Any]], label: str) -> None:
    if len(actual) > len(expected):
        raise Corruption(label + ": event stream exceeds grammar")
    for index, event in enumerate(actual):
        if not _same_event(event, expected[index]):
            raise Corruption(f"{label}: event {index} violates exact order/ref")


@dataclass(frozen=True)
class ChildState:
    slot: str
    task_id: str
    call_ref: dict[str, Any]
    call_bytes: bytes
    receipt_bytes: bytes
    receipt_ref: dict[str, Any]
    planned: bool
    linked: bool
    has_task: bool
    accepted: bool
    receipt_committed: bool
    payload_dir: bool
    payload_file: bool


@dataclass(frozen=True)
class CompositeState:
    root_id: str
    call_ref: dict[str, Any]
    call: dict[str, Any]
    has_task: bool
    accepted: bool
    linked: bool
    first: ChildState
    second: ChildState
    first_observed: bool
    second_observed: bool
    oracle_match: bool
    receipt_bytes: bytes
    receipt_ref: dict[str, Any]
    receipt_committed: bool
    payload_dir: bool
    payload_file: bool

    @property
    def terminal(self) -> bool:
        return self.receipt_committed or (self.first_observed and not self.oracle_match)


@dataclass(frozen=True)
class CompositeProjection:
    root: CompositeState | None
    tails: tuple[TornTail, ...]

    def report(self) -> dict[str, Any]:
        if self.root is None:
            return {"phase": "stable", "roots": []}
        if self.root.receipt_committed:
            state = "receipt_committed"
        elif self.root.first_observed and not self.root.oracle_match:
            state = "waiting_for_parent_policy"
        else:
            state = "progressing"
        return {"phase": "stable", "roots": [{"root_id": self.root.root_id,
                                                  "state": state}]}


class CompositeFailpoints:
    def __init__(self, store: Path, requested: str | None):
        if requested is not None and requested not in ALL_FAKE_FAILPOINTS:
            raise StoreError("unknown sequence_two_fake failpoint: " + requested)
        self.requested = requested
        self.sidecar = store.with_name(store.name + ".failpoint-used")

    def hit(self, point: str) -> None:
        if self.requested != point:
            return
        prior = read_regular(self.sidecar, 256, "failpoint sidecar", required=False)
        if prior is not None:
            if prior != (point + "\n").encode("ascii"):
                raise Corruption("failpoint sidecar content mismatch")
            return
        os._exit(97)


class CompositeFakeRuntime(RootRuntime):
    """Single-writer eager fixture runner; deliberately not a public scheduler."""

    def __init__(self, store: str | Path, *, failpoint: str | None = None):
        self.paths = StorePaths(store)
        self.failpoints = CompositeFailpoints(self.paths.root, failpoint)

    def _task_entries(self, ident: str) -> tuple[Path, set[str]]:
        folder = self._task_folder(ident)
        entries = list_directory(folder, "reachable fake Task")
        names: set[str] = set()
        for entry in entries:
            names.add(entry.name)
            if entry.name in (CALL, TASK, EVENTS):
                continue
            if entry.name == PAYLOAD:
                if entry.is_symlink() or not entry.is_dir(follow_symlinks=False):
                    raise Corruption("payload must be an immediate non-symlink directory")
                continue
            if any(is_temp_for(entry.name, final) for final in (CALL, TASK, EVENTS)):
                read_regular(folder / entry.name, MAX_JSON, "Task atomic temp")
                continue
            raise Corruption("forbidden reachable fake Task entry: " + entry.name)
        return folder, names

    def _payload(self, folder: Path, receipt: bytes, *, allowed: bool,
                 committed: bool) -> tuple[bool, bool]:
        path = folder / PAYLOAD
        try:
            info = os.lstat(path)
        except FileNotFoundError:
            if committed:
                raise Corruption("committed Receipt lacks payload directory")
            return False, False
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
            raise Corruption("payload must be immediate non-symlink directory")
        if not allowed:
            raise Corruption("payload exists before its durable stage")
        final = make_ref(receipt)["sha256"] + ".receipt.json"
        present = False
        for entry in list_directory(path, "Receipt payload"):
            if entry.name == final:
                data = read_regular(path / final, MAX_JSON, "Receipt payload")
                if data != receipt:
                    raise Corruption("Receipt payload bytes/ref mismatch")
                present = True
            elif is_temp_for(entry.name, final):
                read_regular(path / entry.name, MAX_JSON, "Receipt payload temp")
            else:
                raise Corruption("unexpected Receipt payload entry: " + entry.name)
        if committed and not present:
            raise Corruption("receipt_committed lacks matching payload")
        return True, present

    def _child_state(self, root_call: dict[str, Any], slot: str, *, planned: bool,
                     linked: bool, tails: list[TornTail]) -> ChildState:
        ident = derive_child_id(self._current_root_id, slot)
        call_bytes = fake_call_bytes(root_call, slot)
        call_ref = make_ref(call_bytes)
        result = root_call["children"][SLOTS.index(slot)]["recipe"]["result"]
        receipt = fake_leaf_receipt_bytes(ident, call_ref, slot, result)
        receipt_ref = make_ref(receipt)
        if not planned:
            return ChildState(slot, ident, call_ref, call_bytes, receipt, receipt_ref,
                              False, False, False, False, False, False, False)
        folder, _ = self._task_entries(ident)
        actual_call = read_regular(folder / CALL, MAX_JSON, slot + " call.json")
        if actual_call != call_bytes:
            raise Corruption(slot + " planned Call differs from frozen recipe")
        task_data = read_regular(folder / TASK, MAX_JSON, slot + " task.json", required=False)
        has_task = task_data is not None
        if has_task:
            assert task_data is not None
            _validate_task(task_data, ident, call_ref)
        log = read_json_log(folder / EVENTS, slot + " events", required=False)
        if log.tail is not None:
            tails.append(log.tail)
        expected = [{"v": 1, "type": "accepted"},
                    {"v": 1, "type": "receipt_committed", "receipt_ref": receipt_ref}]
        _is_prefix(log.events, expected, slot + " events")
        accepted = len(log.events) >= 1
        receipt_committed = len(log.events) == 2
        if not linked and receipt_committed:
            raise Corruption("unlinked fake child cannot commit Receipt")
        if linked and (not has_task or not accepted):
            raise Corruption("linked fake child lacks task.json or accepted")
        payload_dir, payload_file = self._payload(
            folder, receipt, allowed=linked and accepted, committed=receipt_committed)
        return ChildState(slot, ident, call_ref, call_bytes, receipt, receipt_ref,
                          True, linked, has_task, accepted, receipt_committed,
                          payload_dir, payload_file)

    def validate_store(self, *, candidate_id: str | None = None) -> CompositeProjection:
        self._validate_store_names()
        roots = read_json_log(self.paths.root / ROOTS, "roots.jsonl", required=False)
        if len(roots.events) > 2:
            raise Corruption("fixture permits one root planned->linked")
        planned: tuple[str, dict[str, Any]] | None = None
        linked = False
        for index, event in enumerate(roots.events):
            typ, ident, call_ref = _root_event(event)
            if index == 0:
                if typ != "root_planned":
                    raise Corruption("root registry must begin root_planned")
                planned = (ident, call_ref)
            else:
                assert planned is not None
                if typ != "root_linked" or ident != planned[0] or call_ref != planned[1]:
                    raise Corruption("root_linked mismatch")
                linked = True
        tails: list[TornTail] = []
        if roots.tail is not None:
            tails.append(roots.tail)
        view: CompositeState | None = None
        if planned is not None:
            ident, call_ref = planned
            self._current_root_id = ident
            folder, _ = self._task_entries(ident)
            call_data = read_regular(folder / CALL, MAX_JSON, "fake root call.json")
            assert call_data is not None
            call = _authority_call(call_data, "fake root call.json")
            if make_ref(call_data) != call_ref:
                raise Corruption("fake root call_ref mismatch")
            task_data = read_regular(folder / TASK, MAX_JSON, "root task.json", required=False)
            has_task = task_data is not None
            if has_task:
                assert task_data is not None
                _validate_task(task_data, ident, call_ref)

            child_call_refs = {slot: make_ref(fake_call_bytes(call, slot)) for slot in SLOTS}
            child_receipts = {
                slot: fake_leaf_receipt_bytes(derive_child_id(ident, slot),
                    child_call_refs[slot], slot,
                    call["children"][SLOTS.index(slot)]["recipe"]["result"])
                for slot in SLOTS
            }
            child_receipt_refs = {slot: make_ref(child_receipts[slot]) for slot in SLOTS}
            root_receipt = composite_receipt_bytes(
                ident, call_ref, child_receipt_refs["first"], child_receipt_refs["second"])
            root_receipt_ref = make_ref(root_receipt)
            oracle_match = (call["children"][0]["recipe"]["result"]
                            == call["first_success_oracle"])
            expected = [{"v": 1, "type": "accepted"}]
            for slot in SLOTS:
                if slot == "second" and not oracle_match:
                    break
                child_id = derive_child_id(ident, slot)
                expected.extend([
                    {"v": 1, "type": "child_planned", "slot": slot,
                     "task_id": child_id, "call_ref": child_call_refs[slot]},
                    {"v": 1, "type": "child_linked", "slot": slot,
                     "task_id": child_id, "call_ref": child_call_refs[slot]},
                    {"v": 1, "type": "child_observed", "slot": slot,
                     "task_id": child_id, "receipt_ref": child_receipt_refs[slot]},
                ])
            if oracle_match:
                expected.append({"v": 1, "type": "receipt_committed",
                                 "receipt_ref": root_receipt_ref})
            log = read_json_log(folder / EVENTS, "root events", required=False)
            if log.tail is not None:
                tails.append(log.tail)
            _is_prefix(log.events, expected, "root events")
            accepted = len(log.events) >= 1
            if linked and (not has_task or not accepted):
                raise Corruption("linked root lacks task.json or accepted")
            if not linked and len(log.events) > 1:
                raise Corruption("unlinked root has child relation")
            first_planned, first_linked, first_observed = (len(log.events) >= 2,
                                                           len(log.events) >= 3,
                                                           len(log.events) >= 4)
            second_planned, second_linked, second_observed = (len(log.events) >= 5,
                                                               len(log.events) >= 6,
                                                               len(log.events) >= 7)
            first = self._child_state(call, "first", planned=first_planned,
                                      linked=first_linked, tails=tails)
            second = self._child_state(call, "second", planned=second_planned,
                                       linked=second_linked, tails=tails)
            if first_observed and not first.receipt_committed:
                raise Corruption("parent observed uncommitted first Receipt")
            if second_observed and not second.receipt_committed:
                raise Corruption("parent observed uncommitted second Receipt")
            receipt_committed = len(log.events) == 8
            payload_dir, payload_file = self._payload(
                folder, root_receipt, allowed=second_observed,
                committed=receipt_committed)
            view = CompositeState(ident, call_ref, call, has_task, accepted,
                                  linked, first, second, first_observed,
                                  second_observed, oracle_match, root_receipt,
                                  root_receipt_ref, receipt_committed,
                                  payload_dir, payload_file)
            # The next derived staging directory is part of the current gap.
            # Screen it during whole-projection validation, before any torn-tail
            # repair is allowed to write.
            if linked and not first.planned:
                self._child_candidate(first)
            elif (linked and first_observed and oracle_match
                  and not second.planned):
                self._child_candidate(second)
        if candidate_id is not None and (view is None or view.root_id != candidate_id):
            self._validate_candidate(candidate_id)
        return CompositeProjection(view, tuple(tails))

    def _repair_tails(self, projection: CompositeProjection,
                      *, candidate_id: str | None = None) -> CompositeProjection:
        if not projection.tails:
            return projection
        for tail in projection.tails:
            truncate_verified_tail(tail)
        return self.validate_store(candidate_id=candidate_id)

    def _mkdir_bounded(self, path: Path) -> None:
        if len(list_directory(path.parent, "mkdir parent")) >= MAX_DIRECTORY_ENTRIES:
            raise CapacityUnavailable("capacity unavailable for directory")
        path.mkdir()
        fsync_directory(path.parent)

    def _publish_bounded(self, path: Path, data: bytes) -> None:
        if read_regular(path, max(MAX_JSON, len(data)), "existing publish", required=False) == data:
            return
        if len(list_directory(path.parent, "publish parent")) >= MAX_DIRECTORY_ENTRIES:
            raise CapacityUnavailable("capacity unavailable for atomic temp")
        atomic_publish(path, data)

    def _append_bounded(self, path: Path, value: dict[str, Any]) -> None:
        if not os.path.lexists(path):
            if len(list_directory(path.parent, "log parent")) >= MAX_DIRECTORY_ENTRIES:
                raise CapacityUnavailable("capacity unavailable for event log")
        append_json_line(path, value)

    def _child_candidate(self, child: ChildState) -> Path | None:
        path = self.paths.tasks / child.task_id
        try:
            info = os.lstat(path)
        except FileNotFoundError:
            return None
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
            raise StagingCollision("child staging must be non-symlink directory")
        for entry in list_directory(path, "child staging"):
            if entry.name == CALL or is_temp_for(entry.name, CALL):
                read_regular(path / entry.name, MAX_JSON, "child staging file")
            else:
                raise StagingCollision("child staging collision: " + entry.name)
        return path

    def _progress_child(self, root: CompositeState, child: ChildState) -> bool:
        slot = child.slot
        folder = self.paths.tasks / child.task_id
        if not child.planned:
            candidate = self._child_candidate(child)
            if candidate is None:
                self._mkdir_bounded(folder)
                self.failpoints.hit(f"{slot}_task_dir_after")
                return True
            current = read_regular(folder / CALL, MAX_JSON, "child staged Call", required=False)
            if current != child.call_bytes:
                for entry in list_directory(folder, "child staging"):
                    os.unlink(folder / entry.name); fsync_directory(folder)
                self._publish_bounded(folder / CALL, child.call_bytes)
                self.failpoints.hit(f"{slot}_call_after")
                return True
            self._append_bounded(self.paths.tasks / root.root_id / EVENTS,
                {"v": 1, "type": "child_planned", "slot": slot,
                 "task_id": child.task_id, "call_ref": child.call_ref})
            self.failpoints.hit(f"{slot}_parent_planned_after")
            return True
        if not child.has_task:
            self._publish_bounded(folder / TASK, _task_bytes(child.task_id, child.call_ref))
            self.failpoints.hit(f"{slot}_task_after")
            return True
        if not child.accepted:
            self._append_bounded(folder / EVENTS, {"v": 1, "type": "accepted"})
            self.failpoints.hit(f"{slot}_accepted_after")
            return True
        if not child.linked:
            self._append_bounded(self.paths.tasks / root.root_id / EVENTS,
                {"v": 1, "type": "child_linked", "slot": slot,
                 "task_id": child.task_id, "call_ref": child.call_ref})
            self.failpoints.hit(f"{slot}_parent_linked_after")
            return True
        if not child.payload_dir:
            self._mkdir_bounded(folder / PAYLOAD)
            self.failpoints.hit(f"{slot}_payload_dir_after")
            return True
        if not child.payload_file:
            self._publish_bounded(receipt_path(folder, child.receipt_bytes), child.receipt_bytes)
            self.failpoints.hit(f"{slot}_receipt_payload_after")
            return True
        if not child.receipt_committed:
            self._append_bounded(folder / EVENTS,
                {"v": 1, "type": "receipt_committed", "receipt_ref": child.receipt_ref})
            self.failpoints.hit(f"{slot}_receipt_committed_after")
            return True
        observed = root.first_observed if slot == "first" else root.second_observed
        if not observed:
            self._append_bounded(self.paths.tasks / root.root_id / EVENTS,
                {"v": 1, "type": "child_observed", "slot": slot,
                 "task_id": child.task_id, "receipt_ref": child.receipt_ref})
            self.failpoints.hit(f"{slot}_parent_observed_after")
            return True
        return False

    def _progress_once(self, projection: CompositeProjection) -> bool:
        root = projection.root
        if root is None or root.terminal:
            return False
        folder = self.paths.tasks / root.root_id
        if not root.has_task:
            self.failpoints.hit("root_task_before")
            self._publish_bounded(folder / TASK, _task_bytes(root.root_id, root.call_ref))
            self.failpoints.hit("root_task_after"); return True
        if not root.accepted:
            self.failpoints.hit("root_accepted_before")
            self._append_bounded(folder / EVENTS, {"v": 1, "type": "accepted"})
            self.failpoints.hit("root_accepted_after"); return True
        if not root.linked:
            self.failpoints.hit("root_linked_before")
            self._append_root("root_linked", root.root_id, root.call_ref)
            self.failpoints.hit("root_linked_after"); return True
        if not root.first_observed:
            return self._progress_child(root, root.first)
        if not root.oracle_match:
            return False
        if not root.second_observed:
            return self._progress_child(root, root.second)
        if not root.payload_dir:
            self._mkdir_bounded(folder / PAYLOAD)
            self.failpoints.hit("root_payload_dir_after"); return True
        if not root.payload_file:
            self._publish_bounded(receipt_path(folder, root.receipt_bytes), root.receipt_bytes)
            self.failpoints.hit("root_receipt_payload_after"); return True
        self._append_bounded(folder / EVENTS,
            {"v": 1, "type": "receipt_committed", "receipt_ref": root.receipt_ref})
        self.failpoints.hit("root_receipt_committed_after")
        return True

    def _run_to_stable(self, projection: CompositeProjection) -> CompositeProjection:
        for _ in range(40):
            if not self._progress_once(projection):
                return projection
            projection = self.validate_store()
            projection = self._repair_tails(projection)
        raise StoreError("sequence_two_fake progress did not converge")

    def accept_fixture_root(self, ident: str,
                            resolver: Callable[[], Any]) -> CompositeProjection:
        try:
            ident = root_id(ident)
        except Corruption as exc:
            raise AcceptanceRejected(str(exc)) from exc
        child_ids = tuple(derive_child_id(ident, slot) for slot in SLOTS)
        with self.paths.exclusive_lock(create=True) as lock:
            projection = self.validate_store(candidate_id=ident)
            projection = self._repair_tails(projection, candidate_id=ident)
            if projection.root is not None:
                raise AcceptanceRejected("fixture already has a planned root; use recover")
            for child_id in child_ids:
                if os.path.lexists(self.paths.tasks / child_id):
                    raise StagingCollision("derived child directory exists before root plan")
            before = self._candidate_snapshot(ident)
            before_chain = directory_chain_snapshot(self.paths.root, "pre-resolver store path")
            before_tree = bounded_tree_snapshot(self.paths.root, "pre-resolver store")
            error: BaseException | None = None
            data: bytes | None = None
            try:
                data = root_fake_call_bytes(resolver())
            except BaseException as exc:
                error = exc
            lock.assert_current()
            if directory_chain_snapshot(self.paths.root, "post-resolver store path") != before_chain:
                raise Corruption("resolver changed store path") from error
            if bounded_tree_snapshot(self.paths.root, "post-resolver store") != before_tree:
                raise Corruption("resolver changed store tree") from error
            if self._candidate_snapshot(ident) != before:
                raise Corruption("resolver changed root candidate") from error
            if self.validate_store(candidate_id=ident).root is not None:
                raise Corruption("store authority changed during resolver") from error
            if error is not None:
                if isinstance(error, Exception):
                    raise AcceptanceRejected("acceptance resolver failed: " + str(error)) from error
                raise error
            assert data is not None
            entries = len(list_directory(self.paths.tasks, "tasks"))
            missing = (1 if before == ("absent",) else 0) + 2
            if entries + missing > MAX_DIRECTORY_ENTRIES:
                raise CapacityUnavailable("capacity unavailable for root/children")
            self.failpoints.hit("root_call_before")
            self._rebuild_candidate(ident, data)
            self.failpoints.hit("root_call_after")
            call_ref = make_ref(data)
            self.failpoints.hit("root_planned_before")
            self._append_root("root_planned", ident, call_ref)
            self.failpoints.hit("root_planned_after")
            return self._run_to_stable(self.validate_store())

    def recover_store(self) -> CompositeProjection:
        with self.paths.exclusive_lock(create=False):
            projection = self.validate_store()
            projection = self._repair_tails(projection)
            return self._run_to_stable(projection)
