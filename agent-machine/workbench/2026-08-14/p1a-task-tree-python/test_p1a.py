#!/usr/bin/env python3
"""Independent-process fault matrix for the P1a-1 workbench."""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROGRAM = HERE / "aos_p1a.py"
POINTS = (
    "call_payload_before", "call_payload_after", "planned_before", "planned_after",
    "materialize_before", "materialize_after", "linked_before", "linked_after",
    "dispatch_intent_before", "dispatch_intent_after",
    "child_receipt_stage_before", "child_receipt_stage_after",
    "child_receipt_commit_before", "child_receipt_commit_after",
    "parent_observe_before", "parent_observe_after",
    "parent_receipt_stage_before", "parent_receipt_stage_after",
    "parent_receipt_commit_before", "parent_receipt_commit_after",
)


def invoke(root: Path, case: str, fault: str | None = None, armed: bool = False) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, str(PROGRAM), "--root", str(root), "--case", case]
    if fault:
        command += ["--fault-point", fault]
    if armed:
        command += ["--armed"]
    return subprocess.run(command, text=True, capture_output=True, check=False)


class P1aFaultMatrix(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="aos-p1a-", dir="/tmp"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp)

    def recover(self, root: Path, case: str, fault: str | None) -> dict:
        # First process is killed at a durable boundary.  Each subsequent recovery is
        # a fresh process; ten reruns demonstrate idempotence, not power-loss safety.
        first = invoke(root, case, fault, True)
        self.assertIn(first.returncode, (0, 97), first.stderr)
        for _ in range(10):
            got = invoke(root, case, fault)
            self.assertEqual(got.returncode, 0, got.stderr)
        return json.loads((root / "report.json").read_text())

    def assert_common(self, report: dict) -> None:
        self.assertEqual(report["verification_errors"], [])
        ids = [x["task_id"] for x in report["task_tree"]]
        self.assertEqual(len(ids), len(set(ids)))
        for item in report["task_tree"]:
            self.assertLessEqual(item["dispatch_count"], 1)

    def test_success_fault_matrix(self) -> None:
        for point in POINTS:
            with self.subTest(point=point):
                root = self.tmp / ("success-" + point)
                report = self.recover(root, "success", point)
                self.assert_common(report)
                tasks = report["task_tree"]
                # Crashing after intent but before a complete staged fake result is
                # purposely unknown even for the success fixture.  The point of this
                # branch is that recovery refuses an invented resend.
                if point in ("dispatch_intent_after", "child_receipt_stage_before"):
                    self.assertEqual(len(tasks), 2, report)
                    self.assertEqual([t["state"] for t in tasks], ["repair_required", "repair_required"])
                    self.assertEqual(tasks[1]["dispatch_count"], 1)
                else:
                    self.assertEqual(len(tasks), 3)
                    self.assertTrue(all(t["state"] == "completed" for t in tasks), report)
                    self.assertEqual([t["dispatch_count"] for t in tasks[1:]], [1, 1])

    def test_unknown_fault_matrix(self) -> None:
        for point in POINTS:
            with self.subTest(point=point):
                root = self.tmp / ("unknown-" + point)
                report = self.recover(root, "unknown", point)
                self.assert_common(report)
                tasks = report["task_tree"]
                # Some later success-only points are never reached: normal execution
                # converges first, still followed by ten independent recoveries.
                self.assertEqual(len(tasks), 2, report)
                self.assertEqual(tasks[0]["state"], "repair_required")
                self.assertEqual(tasks[1]["state"], "repair_required")
                self.assertEqual(tasks[1]["dispatch_count"], 1)
                self.assertIsNone(tasks[0]["receipt_hash"])
                self.assertIsNone(tasks[1]["receipt_hash"])

    def test_same_child_id_different_reference_fails_closed(self) -> None:
        root = self.tmp / "corruption"
        self.assertEqual(invoke(root, "success").returncode, 0)
        report = json.loads((root / "report.json").read_text())
        child_id = report["task_tree"][1]["task_id"]
        task = root / "tasks" / child_id / "task.json"
        obj = json.loads(task.read_text())
        obj["call_hash"] = "b" * 64
        task.write_text(json.dumps(obj))
        result = invoke(root, "success")
        self.assertEqual(result.returncode, 2)

    def test_planned_hash_tamper_fails_closed(self) -> None:
        root = self.tmp / "plan-corruption"
        self.assertEqual(invoke(root, "success").returncode, 0)
        parent = root / "tasks" / ("a" * 64) / "events.jsonl"
        lines = parent.read_text().splitlines()
        for index, line in enumerate(lines):
            item = json.loads(line)
            if item["type"] == "child_planned":
                item["call_hash"] = "b" * 64
                lines[index] = json.dumps(item, sort_keys=True, separators=(",", ":"))
                break
        parent.write_text("\n".join(lines) + "\n")
        self.assertEqual(invoke(root, "success").returncode, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
