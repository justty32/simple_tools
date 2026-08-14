"""Root-only Phase 1 model for the P1a-2 prototype."""
from __future__ import annotations

import base64
import binascii
import errno
import os
import re
import signal
import stat
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Callable

from p1a2_store import (
    AcceptanceRejected, Corruption, LogRead, MAX_DIRECTORY_ENTRIES, MAX_JSON,
    StoreError, StorePaths, TornTail, append_json_line, atomic_publish,
    bounded_tree_snapshot, canonical_json, directory_chain_snapshot,
    ensure_canonical_size, exact_fields, fsync_directory, integer, is_temp_for,
    list_directory, make_ref, read_json_log, read_regular, root_id, strict_json,
    task_id, text_value, truncate_verified_tail, validate_ref,
)

ROOTS = "roots.jsonl"
CALL = "call.json"
TASK = "task.json"
EVENTS = "events.jsonl"
STORE_NAMES = {ROOTS, "tasks", "store.lock"}
ROOT_FAILPOINTS = (
    "root_call_before", "root_call_after", "root_planned_before", "root_planned_after",
    "root_task_before", "root_task_after", "root_accepted_before", "root_accepted_after",
    "root_linked_before", "root_linked_after",
)


def _path(value: Any, label: str) -> str:
    value = text_value(value, label)
    try:
        size = len(value.encode("utf-8"))
    except UnicodeEncodeError as exc:
        raise Corruption(label + ": invalid Unicode") from exc
    if "\x00" in value or size > 4096 or not PurePosixPath(value).is_absolute():
        raise Corruption(label + ": absolute POSIX path required")
    return value


def _bytes(value: Any, label: str) -> bytes:
    value = exact_fields(value, {"encoding", "data"}, label)
    if value["encoding"] != "base64" or not isinstance(value["data"], str) or any(ord(c) > 127 for c in value["data"]):
        raise Corruption(label + ": canonical base64 required")
    if len(value["data"]) > MAX_JSON:
        raise Corruption(label + ": encoded bytes exceed Call budget")
    try:
        raw = base64.b64decode(value["data"], validate=True)
    except (binascii.Error, ValueError) as exc:
        raise Corruption(label + ": canonical base64 required") from exc
    if base64.b64encode(raw).decode("ascii") != value["data"]:
        raise Corruption(label + ": non-canonical base64")
    if len(raw) > 8 * 1024 * 1024:
        raise Corruption(label + ": bytes exceed 8 MiB")
    return raw


def _termination(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or not isinstance(value.get("kind"), str):
        raise Corruption(label + ": invalid termination")
    kind = value["kind"]
    if kind == "exited":
        exact_fields(value, {"kind", "code"}, label)
        integer(value["code"], label + ".code", 0, 255)
    elif kind == "signaled":
        exact_fields(value, {"kind", "signal", "signal_name"}, label)
        number = integer(value["signal"], label + ".signal", 1, 255)
        try:
            expected = signal.Signals(number).name
        except ValueError as exc:
            raise Corruption(label + ": unknown signal") from exc
        if value["signal_name"] != expected:
            raise Corruption(label + ": signal name mismatch")
    elif kind == "spawn_error":
        exact_fields(value, {"kind", "stage", "errno", "errno_name"}, label)
        if value["stage"] != "spawn":
            raise Corruption(label + ": spawn stage required")
        number = integer(value["errno"], label + ".errno")
        if value["errno_name"] != errno.errorcode.get(number, "UNKNOWN"):
            raise Corruption(label + ": errno name mismatch")
    else:
        raise Corruption(label + ": unknown termination kind")
    return value


def _bytes_result(value: Any, label: str) -> tuple[dict[str, Any], bytes, bytes]:
    value = exact_fields(value, {"termination", "stdout", "stderr"}, label)
    termination = _termination(value["termination"], label + ".termination")
    stdout = _bytes(value["stdout"], label + ".stdout")
    stderr = _bytes(value["stderr"], label + ".stderr")
    return termination, stdout, stderr


def validate_root_call(value: Any) -> dict[str, Any]:
    ensure_canonical_size(value, MAX_JSON, "root Call")
    value = exact_fields(value, {"v", "kind", "children", "first_success_oracle"}, "root Call")
    if integer(value["v"], "root Call.v", 1, 1) != 1 or value["kind"] != "sequence_two":
        raise Corruption("root Call: v/kind mismatch")
    children = value["children"]
    if not isinstance(children, list) or len(children) != 2:
        raise Corruption("root Call: exactly two children required")
    first = exact_fields(children[0], {"slot", "recipe"}, "first child recipe wrapper")
    second = exact_fields(children[1], {"slot", "recipe"}, "second child recipe wrapper")
    if first["slot"] != "first" or second["slot"] != "second":
        raise Corruption("root Call: child order must be first,second")
    process = exact_fields(first["recipe"], {"kind", "definition_path", "argv", "stdin", "cwd", "environment_policy"}, "process recipe")
    if process["kind"] != "process":
        raise Corruption("process recipe kind mismatch")
    _path(process["definition_path"], "definition_path")
    _path(process["cwd"], "cwd")
    argv = process["argv"]
    if not isinstance(argv, list) or not argv or len(argv) > 256:
        raise Corruption("argv: 1..256 strings required")
    for index, item in enumerate(argv):
        item = text_value(item, f"argv[{index}]")
        try:
            size = len(item.encode("utf-8"))
        except UnicodeEncodeError as exc:
            raise Corruption(f"argv[{index}]: invalid Unicode") from exc
        if "\x00" in item or size > 64 * 1024:
            raise Corruption(f"argv[{index}]: invalid size or NUL")
    _bytes(process["stdin"], "process stdin")
    if process["environment_policy"] != {"kind": "inherit"}:
        raise Corruption("environment policy must be exact inherit")
    fake = exact_fields(second["recipe"], {"kind", "result"}, "fake recipe")
    if fake["kind"] != "fake":
        raise Corruption("fake recipe kind mismatch")
    _bytes_result(fake["result"], "fake result")
    oracle_term, _, oracle_stderr = _bytes_result(value["first_success_oracle"], "first success oracle")
    if oracle_term != {"kind": "exited", "code": 0} or oracle_stderr != b"":
        raise Corruption("first success oracle must be exited(0) with empty stderr")
    encoded = canonical_json(value)
    if len(encoded) > MAX_JSON:
        raise Corruption("root Call exceeds 1 MiB")
    return value


def root_call_bytes(value: Any) -> bytes:
    return canonical_json(validate_root_call(value))


def _call_from_authority(data: bytes, label: str) -> dict[str, Any]:
    obj = strict_json(data, label)
    if not isinstance(obj, dict) or canonical_json(obj) != data:
        raise Corruption(label + ": P1 Call bytes are not canonical")
    return validate_root_call(obj)


def _task_bytes(ident: str, call_ref: dict[str, Any]) -> bytes:
    return canonical_json({"v": 1, "task_id": ident, "call_ref": call_ref})


def _validate_task(data: bytes, ident: str, call_ref: dict[str, Any]) -> None:
    obj = strict_json(data, "task.json")
    if canonical_json(obj) != data:
        raise Corruption("task.json is not canonical")
    obj = exact_fields(obj, {"v", "task_id", "call_ref"}, "task.json")
    if integer(obj["v"], "task.json.v", 1, 1) != 1 or task_id(obj["task_id"]) != ident or validate_ref(obj["call_ref"], "task call_ref") != call_ref:
        raise Corruption("task.json identity/ref mismatch")


def _root_event(event: dict[str, Any]) -> tuple[str, str, dict[str, Any]]:
    event = exact_fields(event, {"v", "type", "root_id", "call_ref"}, "root event")
    if integer(event["v"], "root event.v", 1, 1) != 1 or event["type"] not in ("root_planned", "root_linked"):
        raise Corruption("root event v/type mismatch")
    return event["type"], root_id(event["root_id"]), validate_ref(event["call_ref"], "root event call_ref")


def _accepted_event(event: dict[str, Any]) -> None:
    event = exact_fields(event, {"v", "type"}, "root Task event")
    if integer(event["v"], "root Task event.v", 1, 1) != 1 or event["type"] != "accepted":
        raise Corruption("Phase 1 root Task only permits accepted")


@dataclass(frozen=True)
class RootView:
    root_id: str
    call_ref: dict[str, Any]
    call: dict[str, Any]
    has_task: bool
    accepted: bool
    linked: bool
    task_events: tuple[dict[str, Any], ...]


@dataclass(frozen=True)
class Projection:
    root: RootView | None
    tails: tuple[TornTail, ...]

    def report(self) -> dict[str, Any]:
        if self.root is None:
            return {"phase": "stable", "roots": []}
        state = "linked" if self.root.linked else "accepted" if self.root.accepted else "materialized" if self.root.has_task else "planned"
        return {"phase": "stable", "roots": [{"root_id": self.root.root_id,
                                                  "state": state,
                                                  "call_ref": self.root.call_ref,
                                                  "event_count": len(self.root.task_events)}]}


class Failpoints:
    def __init__(self, store: Path, requested: str | None):
        if requested is not None and requested not in ROOT_FAILPOINTS:
            raise StoreError("unknown/unreachable Phase 1 failpoint: " + requested)
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


class Runtime:
    def __init__(self, store: str | Path, *, failpoint: str | None = None):
        self.paths = StorePaths(store)
        self.failpoints = Failpoints(self.paths.root, failpoint)

    def _validate_store_names(self) -> None:
        for entry in list_directory(self.paths.root, "store"):
            if entry.name not in STORE_NAMES:
                raise Corruption("unknown store entry: " + entry.name)
        read_regular(self.paths.lock_path, 1024, "store.lock")
        for entry in list_directory(self.paths.tasks, "tasks"):
            task_id(entry.name, "tasks entry")
            if entry.is_symlink() or not entry.is_dir(follow_symlinks=False):
                raise Corruption("tasks entry must be immediate non-symlink directory")

    def _task_folder(self, ident: str, *, create: bool = False) -> Path:
        ident = task_id(ident)
        path = self.paths.tasks / ident
        if create and not path.exists():
            path.mkdir()
            fsync_directory(self.paths.tasks)
        try:
            info = os.lstat(path)
        except FileNotFoundError:
            raise Corruption("missing Task candidate: " + ident)
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
            raise Corruption("Task candidate must be non-symlink directory")
        list_directory(path, "Task candidate")
        return path

    def _validate_candidate(self, ident: str) -> Path | None:
        path = self.paths.tasks / task_id(ident)
        try:
            info = os.lstat(path)
        except FileNotFoundError:
            return None
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
            raise AcceptanceRejected("root staging candidate is not a non-symlink directory")
        entries = list_directory(path, "root staging candidate")
        for entry in entries:
            if entry.name == CALL:
                # Before root_planned this is discardable staging, not authority.
                # Only its bounded regular-file shape is trusted; the new resolver
                # result, not these bytes, is validated before whole replacement.
                read_regular(path / CALL, MAX_JSON, "staged root Call")
            elif is_temp_for(entry.name, CALL):
                read_regular(path / entry.name, MAX_JSON, "staged Call temp")
            else:
                raise AcceptanceRejected("root staging candidate anomaly: " + entry.name)
        return path

    def _validate_planned_folder(self, ident: str, call_ref: dict[str, Any], *, linked: bool) -> tuple[dict[str, Any], bool, bool, tuple[dict[str, Any], ...], TornTail | None]:
        folder = self._task_folder(ident)
        allowed = {CALL, TASK, EVENTS}
        entries = list_directory(folder, "planned root Task")
        for entry in entries:
            if entry.name not in allowed and not any(is_temp_for(entry.name, final) for final in allowed):
                raise Corruption("unlinked/root Phase 1 Task has forbidden entry: " + entry.name)
            if entry.is_symlink():
                raise Corruption("symlink rejected in planned Task")
            if entry.name not in allowed:
                read_regular(folder / entry.name, MAX_JSON, "planned Task temp")
        call_data = read_regular(folder / CALL, MAX_JSON, "planned root call.json")
        assert call_data is not None
        call = _call_from_authority(call_data, "planned root call.json")
        if make_ref(call_data) != call_ref:
            raise Corruption("root planned call_ref mismatch")
        task_data = read_regular(folder / TASK, MAX_JSON, "root task.json", required=False)
        has_task = task_data is not None
        if has_task:
            assert task_data is not None
            _validate_task(task_data, ident, call_ref)
        events = read_json_log(folder / EVENTS, "root events", required=False)
        if len(events.events) > 1:
            raise Corruption("Phase 1 root has duplicate/unexpected Task events")
        if events.events:
            _accepted_event(events.events[0])
        accepted = bool(events.events)
        if linked and (not has_task or not accepted):
            raise Corruption("linked root lacks task.json or accepted")
        return call, has_task, accepted, events.events, events.tail

    def validate_store(self, *, candidate_id: str | None = None) -> Projection:
        self._validate_store_names()
        roots = read_json_log(self.paths.root / ROOTS, "roots.jsonl", required=False)
        if len(roots.events) > 2:
            raise Corruption("fixture permits only planned->linked for one root")
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
                    raise Corruption("root_linked does not match root_planned")
                linked = True
        tails: list[TornTail] = []
        if roots.tail is not None:
            tails.append(roots.tail)
        view: RootView | None = None
        if planned is not None:
            ident, call_ref = planned
            call, has_task, accepted, task_events, task_tail = self._validate_planned_folder(ident, call_ref, linked=linked)
            if task_tail is not None:
                tails.append(task_tail)
            view = RootView(ident, call_ref, call, has_task, accepted, linked, task_events)
        if candidate_id is not None and (view is None or view.root_id != candidate_id):
            self._validate_candidate(candidate_id)
        return Projection(view, tuple(tails))

    def _repair_tails(self, projection: Projection, *, candidate_id: str | None = None) -> Projection:
        if not projection.tails:
            return projection
        for tail in projection.tails:
            truncate_verified_tail(tail)
        return self.validate_store(candidate_id=candidate_id)

    def _rebuild_candidate(self, ident: str, data: bytes) -> Path:
        path = self.paths.tasks / task_id(ident)
        if not path.exists():
            path.mkdir()
            fsync_directory(self.paths.tasks)
        self._validate_candidate(ident)
        for entry in list_directory(path, "root staging candidate"):
            if entry.name == CALL or is_temp_for(entry.name, CALL):
                os.unlink(path / entry.name)
                fsync_directory(path)
        atomic_publish(path / CALL, data, replace_staging=True)
        return path

    def _append_root(self, typ: str, ident: str, call_ref: dict[str, Any]) -> None:
        append_json_line(self.paths.root / ROOTS,
                         {"v": 1, "type": typ, "root_id": ident, "call_ref": call_ref})

    def _progress_root_once(self, projection: Projection) -> bool:
        root = projection.root
        if root is None or root.linked:
            return False
        folder = self._task_folder(root.root_id)
        # Atomic temps are non-authority crash debris.  Remove at most one after
        # a whole-projection validation, fsync its directory, then revalidate.
        # This both preserves the one-transition loop and prevents repeated
        # pre-rename crashes from exhausting the 64-entry directory bound.
        for entry in sorted(list_directory(folder, "planned root Task"),
                            key=lambda item: os.fsencode(item.name)):
            if any(is_temp_for(entry.name, final) for final in (CALL, TASK, EVENTS)):
                read_regular(folder / entry.name, MAX_JSON, "planned Task temp")
                os.unlink(folder / entry.name)
                fsync_directory(folder)
                return True
        if not root.has_task:
            self.failpoints.hit("root_task_before")
            atomic_publish(folder / TASK, _task_bytes(root.root_id, root.call_ref))
            self.failpoints.hit("root_task_after")
            return True
        if not root.accepted:
            self.failpoints.hit("root_accepted_before")
            append_json_line(folder / EVENTS, {"v": 1, "type": "accepted"})
            self.failpoints.hit("root_accepted_after")
            return True
        self.failpoints.hit("root_linked_before")
        self._append_root("root_linked", root.root_id, root.call_ref)
        self.failpoints.hit("root_linked_after")
        return True

    def _run_to_stable(self, projection: Projection) -> Projection:
        for _ in range(MAX_DIRECTORY_ENTRIES + 8):
            if not self._progress_root_once(projection):
                return projection
            projection = self.validate_store()
            projection = self._repair_tails(projection)
        raise StoreError("root progress did not converge")

    def accept_fixture_root(self, ident: str, resolver: Callable[[], Any]) -> Projection:
        try:
            ident = root_id(ident)
        except Corruption as exc:
            raise AcceptanceRejected(str(exc)) from exc
        with self.paths.exclusive_lock(create=True) as lock:
            projection = self.validate_store(candidate_id=ident)
            projection = self._repair_tails(projection, candidate_id=ident)
            if projection.root is not None:
                raise AcceptanceRejected("fixture already has a planned root; use recover")
            before = self._candidate_snapshot(ident)
            if before == ("absent",) and len(list_directory(self.paths.tasks, "tasks")) >= MAX_DIRECTORY_ENTRIES:
                raise AcceptanceRejected("tasks directory lacks capacity for root candidate")
            before_chain = directory_chain_snapshot(self.paths.root, "pre-resolver store path")
            before_tree = bounded_tree_snapshot(self.paths.root, "pre-resolver store")
            resolver_error: BaseException | None = None
            data: bytes | None = None
            try:
                value = resolver()
                data = root_call_bytes(value)
            except BaseException as exc:
                resolver_error = exc
            # Every callback exit, including failure, must return through the same
            # authority/lock guard before this process releases its old flock.
            lock.assert_current()
            after_chain = directory_chain_snapshot(self.paths.root, "post-resolver store path")
            if after_chain != before_chain:
                raise Corruption("resolver changed the store path chain") from resolver_error
            after_tree = bounded_tree_snapshot(self.paths.root, "post-resolver store")
            if after_tree != before_tree:
                raise Corruption("resolver changed the store tree") from resolver_error
            if self._candidate_snapshot(ident) != before:
                raise Corruption("resolver changed the root candidate") from resolver_error
            # The callback ran while the lock was held, but it is still outside
            # the trusted store model.  Revalidate authority before the first write.
            post_resolver = self.validate_store(candidate_id=ident)
            if post_resolver.root is not None or post_resolver.tails:
                raise Corruption("store authority changed during acceptance resolver") from resolver_error
            if resolver_error is not None:
                if isinstance(resolver_error, Corruption):
                    raise AcceptanceRejected(str(resolver_error)) from resolver_error
                if isinstance(resolver_error, Exception):
                    raise AcceptanceRejected("acceptance resolver failed: " + str(resolver_error)) from resolver_error
                raise resolver_error
            assert data is not None
            self.failpoints.hit("root_call_before")
            self._rebuild_candidate(ident, data)
            self.failpoints.hit("root_call_after")
            call_ref = make_ref(data)
            self.failpoints.hit("root_planned_before")
            self._append_root("root_planned", ident, call_ref)
            self.failpoints.hit("root_planned_after")
            projection = self.validate_store()
            return self._run_to_stable(projection)

    def _candidate_snapshot(self, ident: str) -> tuple[Any, ...]:
        path = self.paths.tasks / ident
        try:
            folder_info = os.lstat(path)
        except FileNotFoundError:
            return ("absent",)
        if stat.S_ISLNK(folder_info.st_mode) or not stat.S_ISDIR(folder_info.st_mode):
            raise Corruption("resolver changed candidate directory type")
        rows: list[tuple[str, int, int, int, bytes]] = []
        for entry in list_directory(path, "root staging candidate"):
            data = read_regular(path / entry.name, MAX_JSON, "candidate snapshot")
            assert data is not None
            info = os.lstat(path / entry.name)
            rows.append((entry.name, info.st_dev, info.st_ino, info.st_mode, data))
        return ("directory", folder_info.st_dev, folder_info.st_ino, tuple(sorted(rows)))

    def recover_store(self) -> Projection:
        with self.paths.exclusive_lock(create=False):
            projection = self.validate_store()
            projection = self._repair_tails(projection)
            return self._run_to_stable(projection)
