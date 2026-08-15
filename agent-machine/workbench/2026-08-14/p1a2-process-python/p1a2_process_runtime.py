"""Phase 2 progress/effect orchestration; projection is strictly read-only."""
from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Any, Callable

from p1a2_model import ROOTS, Runtime as RootRuntime
from p1a2_process_effects import step
from p1a2_process_state import ATTEMPT, ATTEMPTS, read_state
from p1a2_store import Corruption, MAX_JSON, StoreError, StorePaths, append_json_line, atomic_publish, fsync_directory, is_temp_for, list_directory, read_json_log, read_regular, require_directory, task_id, truncate_verified_tail

P0_SOURCE = Path(__file__).resolve().parents[1] / "p0-function-python"
if os.fspath(P0_SOURCE) not in sys.path: sys.path.insert(0, os.fspath(P0_SOURCE))
from aos_p0 import FunctionStore, Request

PAYLOAD = "payload"


class ProcessRuntime:
    def __init__(self, store: str | Path): self.paths = StorePaths(store)

    def accept_fixture_root(self, ident: str, resolver: Callable[[], Any]) -> dict[str, Any]:
        RootRuntime(self.paths.root).accept_fixture_root(ident, resolver); return self.recover_store()

    def _folder(self, ident: str) -> Path:
        path = self.paths.tasks / ident; require_directory(path, "Task folder"); return path

    def _entries(self, folder: Path, allowed: set[str], label: str) -> set[str]:
        names: set[str] = set()
        for entry in list_directory(folder, label):
            names.add(entry.name)
            if entry.name in allowed:
                if entry.name in (ATTEMPTS, PAYLOAD): require_directory(folder / entry.name, entry.name)
            elif any(is_temp_for(entry.name, item) for item in allowed if item not in (ATTEMPTS, PAYLOAD)):
                read_regular(folder / entry.name, MAX_JSON, label + " temp")
            else: raise Corruption(label + " has forbidden entry: " + entry.name)
        return names

    def _mkdir(self, path: Path) -> None: path.mkdir(); fsync_directory(path.parent)
    def _publish(self, path: Path, data: bytes, *, staging: bool = False) -> None: atomic_publish(path, data, replace_staging=staging)
    def _append(self, path: Path, event: dict[str, Any]) -> None: append_json_line(path, event)

    def _run_p0(self, child: dict[str, Any]) -> None:
        root = child["folder"] / ATTEMPTS
        for path in (root, root / ATTEMPT, root / ATTEMPT / "p0"):
            if not path.exists(): self._mkdir(path)
        call = child["call"]; from p1a2_model import _bytes
        request = Request(call["definition"]["absolute_path"], call["argv"], _bytes(call["stdin"], "Call stdin"), call["cwd"])
        FunctionStore(root / ATTEMPT / "p0").run(request)

    def _layout(self) -> None:
        allowed = {ROOTS, "tasks", "store.lock"}
        for entry in list_directory(self.paths.root, "store"):
            if entry.name not in allowed: raise Corruption("unknown store entry: " + entry.name)
        require_directory(self.paths.tasks, "tasks")
        read_regular(self.paths.lock_path, 1024, "store.lock")
        for entry in list_directory(self.paths.tasks, "tasks"):
            task_id(entry.name, "tasks entry")
            if entry.is_symlink() or not entry.is_dir(follow_symlinks=False): raise Corruption("tasks entry must be non-symlink directory")

    def _root_gap(self) -> bool:
        log = read_json_log(self.paths.root / ROOTS, "roots.jsonl", required=False)
        return len(log.events) < 2 or log.tail is not None

    def recover_store(self) -> dict[str, Any]:
        if self._root_gap(): RootRuntime(self.paths.root).recover_store()
        with self.paths.exclusive_lock(create=False):
            for _ in range(40):
                self._layout()
                view = read_state(self)
                if view["tails"]:
                    for tail in view["tails"]: truncate_verified_tail(tail)
                    continue
                if not step(self, view):
                    first = view["first"]["stage"]
                    state = "receipt_committed" if view["committed"] else "waiting_for_parent_policy" if first == "observed" else "waiting_for_child_repair_incomplete_evidence" if first == "repair_incomplete" else "waiting_for_child_repair_generation" if first == "repair_generation" else "progressing"
                    return {"phase": "stable", "roots": [{"root_id": view["root"], "state": state}]}
            raise StoreError("Phase 2 progress did not converge")
