from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path

from p1a2_model import Runtime as RootRuntime, _task_bytes
from p1a2_process_contract import live_generation, materialize_first
from p1a2_process_runtime import ProcessRuntime
from p1a2_store import Corruption, append_json_line, canonical_json, make_ref
from test_support import exited, root_call


class ProcessGuardrails(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(dir="/tmp"); self.base = Path(self.temp.name)
        self.good, self.evil = self.base / "good", self.base / "evil"; self.counter = self.base / "counter"
        self.good.write_text("#!/usr/bin/python3\nimport sys\nsys.stdout.buffer.write(b'OK\\n')\n", encoding="utf-8")
        self.evil.write_text("#!/usr/bin/python3\nimport pathlib,sys\npathlib.Path(sys.argv[1]).write_text('effect')\n", encoding="utf-8")
        self.good.chmod(0o755); self.evil.chmod(0o755)
        self.call = root_call(str(self.good), str(self.base), b"")
        self.call["children"][0]["recipe"]["argv"] = [str(self.good)]
        self.call["first_success_oracle"] = exited(0, b"OK\n")
        self.store = self.base / "store"; RootRuntime(self.store).accept_fixture_root("root", lambda: self.call)

    def tearDown(self) -> None: self.temp.cleanup()

    def first(self, call: dict | None = None, *, linked: bool = True) -> Path:
        call = materialize_first(self.call) if call is None else call; data = canonical_json(call); ref = make_ref(data)
        root, child = self.store / "tasks" / "root", self.store / "tasks" / "root--first"; child.mkdir()
        (child / "call.json").write_bytes(data); (child / "task.json").write_bytes(_task_bytes("root--first", ref)); append_json_line(child / "events.jsonl", {"v": 1, "type": "accepted"})
        append_json_line(root / "events.jsonl", {"v": 1, "type": "child_planned", "slot": "first", "task_id": "root--first", "call_ref": ref})
        if linked: append_json_line(root / "events.jsonl", {"v": 1, "type": "child_linked", "slot": "first", "task_id": "root--first", "call_ref": ref})
        return child

    def test_frozen_recipe_bypass_has_zero_effect(self) -> None:
        call = materialize_first(self.call); call["definition"]["absolute_path"] = str(self.evil); call["definition"]["generation"]["hex"] = live_generation(str(self.evil)); call["argv"] = [str(self.evil), str(self.counter)]
        self.first(call)
        with self.assertRaises(Corruption): ProcessRuntime(self.store).recover_store()
        self.assertFalse(self.counter.exists()); self.assertFalse((self.store / "tasks" / "root--first" / "attempts").exists())

    def test_accepted_generation_repair_and_late_attempt_are_closed(self) -> None:
        child = self.first(); self.good.write_text("#!/usr/bin/python3\nraise SystemExit(1)\n", encoding="utf-8")
        report = ProcessRuntime(self.store).recover_store()
        self.assertEqual(report["roots"][0]["state"], "waiting_for_child_repair_generation")
        self.assertNotIn("dispatch_intent", (child / "events.jsonl").read_text()); (child / "attempts").mkdir()
        with self.assertRaises(Corruption): ProcessRuntime(self.store).recover_store()

    def test_generation_repair_after_dispatch_is_a_contradiction(self) -> None:
        child = self.first()
        append_json_line(child / "events.jsonl", {"v": 1, "type": "dispatch_intent", "attempt_id": "attempt-1"})
        append_json_line(child / "events.jsonl", {"v": 1, "type": "repair_required", "reason": "generation"})
        with self.assertRaises(Corruption): ProcessRuntime(self.store).recover_store()

    def test_premature_root_and_unlinked_effect_artifacts_fail_before_run(self) -> None:
        (self.store / "tasks" / "root" / "payload").mkdir(); (self.store / "tasks" / "root" / "payload" / "evil").write_bytes(b"x")
        with self.assertRaises(Corruption): ProcessRuntime(self.store).recover_store()
        shutil.rmtree(self.store / "tasks" / "root" / "payload")
        child = self.first(linked=False); (child / "attempts").mkdir()
        with self.assertRaises(Corruption): ProcessRuntime(self.store).recover_store()

    def test_root_planned_gap_is_finished_by_phase1_before_process(self) -> None:
        roots = self.store / "roots.jsonl"; roots.write_bytes(roots.read_bytes().splitlines()[0] + b"\n")
        self.assertEqual(ProcessRuntime(self.store).recover_store()["roots"][0]["state"], "receipt_committed")


if __name__ == "__main__": unittest.main(verbosity=2)
