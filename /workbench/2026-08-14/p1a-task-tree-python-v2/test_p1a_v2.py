#!/usr/bin/env python3
"""P1a v2 crash matrix and corruption checks; runs each writer in a new process."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROGRAM = HERE / "aos_p1a_v2.py"
ROOT_ID = "a" * 64
POINTS = (
    "call_payload_before", "call_payload_after", "planned_before", "planned_after",
    "materialize_before", "materialize_after", "linked_before", "linked_after",
    "materialize_task_after", "accepted_before", "accepted_after",
    "dispatch_intent_before", "dispatch_intent_after",
    "child_receipt_stage_before", "child_receipt_stage_after",
    "child_receipt_commit_before", "child_receipt_commit_after",
    "parent_observe_before", "parent_observe_after",
    "parent_receipt_stage_before", "parent_receipt_stage_after",
    "parent_receipt_commit_before", "parent_receipt_commit_after",
)


def run(root: Path, case: str = "success", fault: str | None = None, armed: bool = False,
        timeout: float | None = None):
    cmd = [sys.executable, str(PROGRAM), "--root", str(root), "--case", case]
    if fault:
        cmd += ["--fault-point", fault]
    if armed:
        cmd += ["--armed"]
    return subprocess.run(cmd, text=True, capture_output=True, check=False, timeout=timeout)


def child_id(slot: str) -> str:
    import hashlib
    return hashlib.sha256(b"aos-p1a-child/v2\0" + ROOT_ID.encode() + b"\0" + slot.encode()).hexdigest()


def report(root: Path) -> dict:
    normal = root / "report.json"
    sidecar = Path(str(root) + ".corrupt-report.json")
    return json.loads((sidecar if sidecar.exists() else normal).read_text())


def events(root: Path, ident: str) -> list[dict]:
    return [json.loads(x) for x in (root / "tasks" / ident / "events.jsonl").read_text().splitlines()]


class P1aV2(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="aos-p1a-v2-", dir="/tmp"))

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def recover(self, name: str, case: str, point: str | None = None) -> dict:
        root = self.tmp / name
        first = run(root, case, point, True)
        self.assertIn(first.returncode, (0, 97), first.stderr)
        # Ten separate replay processes prove idempotence, not power-loss safety.
        for _ in range(10):
            got = run(root, case, point)
            self.assertEqual(got.returncode, 0, got.stderr)
        got = report(root)
        self.assertEqual(got["verification_errors"], [], got)
        return got

    def tree(self, got: dict) -> tuple[dict, list[dict]]:
        return got["task_tree"]["root"], got["task_tree"]["children"]

    def test_success_fault_matrix(self):
        for point in POINTS:
            with self.subTest(point=point):
                got = self.recover("s-" + point, "success", point)
                root, children = self.tree(got)
                if point in ("dispatch_intent_after", "child_receipt_stage_before"):
                    self.assertEqual(root["state"], "repair_required")
                    self.assertEqual(len(children), 1)
                    self.assertEqual(children[0]["state"], "repair_required")
                    self.assertEqual(children[0]["dispatch_count"], 1)
                else:
                    self.assertEqual(root["state"], "completed")
                    self.assertEqual([x["state"] for x in children], ["completed", "completed"])
                    self.assertEqual([x["dispatch_count"] for x in children], [1, 1])

    def test_unknown_fault_matrix(self):
        for point in POINTS:
            with self.subTest(point=point):
                got = self.recover("u-" + point, "unknown", point)
                root, children = self.tree(got)
                self.assertEqual(root["state"], "repair_required")
                self.assertEqual(len(children), 1)
                self.assertEqual(children[0]["state"], "repair_required")
                self.assertEqual(children[0]["dispatch_count"], 1)

    def completed(self, name: str) -> Path:
        root = self.tmp / name
        self.assertEqual(run(root).returncode, 0)
        return root

    def corrupt(self, root: Path):
        got = run(root)
        self.assertEqual(got.returncode, 2, got.stderr)
        self.assertEqual(report(root)["phase"], "corrupt")
        self.assertTrue(report(root)["verification_errors"])

    def alter_parent_event(self, root: Path, predicate, change):
        path = root / "tasks" / ROOT_ID / "events.jsonl"
        lines = [json.loads(x) for x in path.read_text().splitlines()]
        for item in lines:
            if predicate(item):
                change(item); break
        path.write_text("\n".join(json.dumps(x, sort_keys=True, separators=(",", ":")) for x in lines) + "\n")

    def test_same_call_bytes_but_distinct_tasks_and_no_relation_in_call(self):
        root = self.completed("same-call")
        children = report(root)["task_tree"]["children"]
        one = (root / "tasks" / children[0]["task_id"] / "call.json").read_bytes()
        two = (root / "tasks" / children[1]["task_id"] / "call.json").read_bytes()
        self.assertEqual(one, two)
        self.assertNotEqual(children[0]["task_id"], children[1]["task_id"])
        obj = json.loads(one)
        self.assertFalse({"task_id", "parent_id", "slot", "state"} & set(obj))

    def test_linked_and_observed_tamper_fail_closed(self):
        for kind, field in (("child_linked", "task_id"), ("child_linked", "call_hash"),
                            ("child_observed", "task_id"), ("child_observed", "receipt_hash")):
            with self.subTest(kind=kind, field=field):
                root = self.completed(kind + field)
                self.alter_parent_event(root, lambda e: e["type"] == kind,
                                        lambda e: e.__setitem__(field, "b" * 64))
                self.corrupt(root)

    def test_leaf_receipt_fields_fail_even_if_hash_recomputed(self):
        for field in ("task_id", "call_hash", "basis", "return"):
            with self.subTest(field=field):
                root = self.completed("leaf-" + field)
                child = report(root)["task_tree"]["children"][0]
                old_hash = child["receipt_hash"]
                path = root / "tasks" / child["task_id"] / "payload" / (old_hash + ".receipt.json")
                obj = json.loads(path.read_text())
                if field == "task_id": obj[field] = "b" * 64
                elif field == "call_hash": obj[field] = "b" * 64
                elif field == "basis": obj[field] = {"kind": "attempt", "attempt_id": "attempt-x"}
                else: obj[field]["stdout"]["data"] = "evil\n"
                data = json.dumps(obj, sort_keys=True, separators=(",", ":")).encode()
                new_hash = __import__("hashlib").sha256(data).hexdigest()
                new_path = path.with_name(new_hash + ".receipt.json")
                new_path.write_bytes(data); path.unlink()
                self.alter_parent_event(root, lambda e: e["type"] == "child_observed" and e["task_id"] == child["task_id"], lambda e: e.__setitem__("receipt_hash", new_hash))
                # Child's own committed event is authority too.
                ep = root / "tasks" / child["task_id"] / "events.jsonl"
                rows = [json.loads(x) for x in ep.read_text().splitlines()]
                rows[-1]["receipt_hash"] = new_hash
                ep.write_text("\n".join(json.dumps(x, sort_keys=True, separators=(",", ":")) for x in rows) + "\n")
                self.corrupt(root)

    def test_parent_receipt_child_refs_and_order_tamper(self):
        for variant in ("id", "order"):
            with self.subTest(variant=variant):
                root = self.completed("parent-" + variant)
                parent = report(root)["task_tree"]["root"]
                path = root / "tasks" / ROOT_ID / "payload" / (parent["receipt_hash"] + ".receipt.json")
                obj = json.loads(path.read_text())
                if variant == "id": obj["basis"]["items"][0]["task_id"] = "b" * 64
                else: obj["basis"]["items"].reverse()
                data = json.dumps(obj, sort_keys=True, separators=(",", ":")).encode()
                digest = __import__("hashlib").sha256(data).hexdigest()
                path.with_name(digest + ".receipt.json").write_bytes(data); path.unlink()
                ep = root / "tasks" / ROOT_ID / "events.jsonl"
                rows = [json.loads(x) for x in ep.read_text().splitlines()]; rows[-1]["receipt_hash"] = digest
                ep.write_text("\n".join(json.dumps(x, sort_keys=True, separators=(",", ":")) for x in rows) + "\n")
                self.corrupt(root)

    def test_event_reverse_duplicate_out_of_order_and_terminal_append(self):
        variants = ("reverse", "duplicate", "out_of_order", "terminal_append")
        for variant in variants:
            with self.subTest(variant=variant):
                root = self.completed("events-" + variant)
                path = root / "tasks" / ROOT_ID / "events.jsonl"
                rows = [json.loads(x) for x in path.read_text().splitlines()]
                if variant == "reverse": rows[1], rows[2] = rows[2], rows[1]
                elif variant == "duplicate": rows.insert(2, dict(rows[1]))
                elif variant == "out_of_order": rows.insert(1, {"schema": "aos-p1a-event/v2", "type": "child_observed", "slot": "first", "task_id": "b" * 64, "receipt_hash": "b" * 64})
                else: rows.append({"schema": "aos-p1a-event/v2", "type": "repair_required", "reason": "late"})
                path.write_text("\n".join(json.dumps(x, sort_keys=True, separators=(",", ":")) for x in rows) + "\n")
                self.corrupt(root)

    def test_torn_tail_truncates_but_bad_newline_json_corrupts(self):
        root = self.completed("torn")
        path = root / "tasks" / ROOT_ID / "events.jsonl"
        old = path.read_bytes(); path.write_bytes(old + b'{"schema"')
        # Pure report accepts the committed prefix but must not mutate its tail.
        check = subprocess.run([sys.executable, str(PROGRAM), "--root", str(root), "--case", "success", "--report-only"], text=True, capture_output=True)
        self.assertEqual(check.returncode, 0, check.stderr)
        self.assertEqual(path.read_bytes(), old + b'{"schema"')
        self.assertEqual(run(root).returncode, 0)
        self.assertTrue(path.read_bytes().endswith(b"\n"))
        root = self.completed("bad-json")
        path = root / "tasks" / ROOT_ID / "events.jsonl"
        path.write_bytes(path.read_bytes() + b'{"schema"}\n')
        self.corrupt(root)

    def test_symlinks_root_tasks_task_payload_and_authority_files(self):
        cases = ("root", "tasks", "task", "payload", "task_json", "events", "call", "fixture")
        for kind in cases:
            with self.subTest(kind=kind):
                root = self.completed("sym-" + kind)
                if kind == "root":
                    target = self.tmp / "real-root"; root.rename(target); os.symlink(target, root)
                elif kind == "tasks":
                    target = root / "tasks-real"; (root / "tasks").rename(target); os.symlink(target, root / "tasks")
                else:
                    base = root / "tasks" / ROOT_ID
                    name = {"task": None, "payload": "payload", "task_json": "task.json", "events": "events.jsonl", "call": "call.json", "fixture": "fixture.json"}[kind]
                    target = root / name if kind == "fixture" else (base if name is None else base / name)
                    moved = target.with_name(target.name + "-real"); target.rename(moved); os.symlink(moved, target)
                self.corrupt(root)

    def test_orphan_child_not_tree_or_dispatch(self):
        root = self.completed("orphan")
        orphan = root / "tasks" / ("c" * 64); orphan.mkdir(); (orphan / "payload").mkdir()
        (orphan / "call.json").write_bytes((root / "tasks" / ROOT_ID / "call.json").read_bytes())
        self.assertEqual(run(root).returncode, 0)
        self.assertNotIn("c" * 64, [x["task_id"] for x in report(root)["task_tree"]["children"]])

    def test_materialized_before_accepted_recovers_then_links(self):
        got = self.recover("h1", "success", "materialize_task_after")
        root, children = self.tree(got)
        self.assertEqual(root["state"], "completed")
        self.assertEqual([x["state"] for x in children], ["completed", "completed"])

    def test_unlinked_child_dispatch_or_receipt_is_corruption(self):
        for evil in ("dispatch", "receipt"):
            with self.subTest(evil=evil):
                root = self.tmp / ("h2-" + evil)
                # Stop after child acceptance but before parent link, then inject.
                self.assertIn(run(root, fault="linked_before", armed=True).returncode, (0, 97))
                child = child_id("first")
                path = root / "tasks" / child / "events.jsonl"
                rows = [json.loads(x) for x in path.read_text().splitlines()]
                if evil == "dispatch":
                    rows.append({"schema": "aos-p1a-event/v2", "type": "dispatch_intent", "attempt_id": "attempt-1"})
                else:
                    rows.append({"schema": "aos-p1a-event/v2", "type": "receipt_committed", "receipt_hash": "b" * 64})
                path.write_text("\n".join(json.dumps(x, sort_keys=True, separators=(",", ":")) for x in rows) + "\n")
                self.corrupt(root)

    def test_raw_file_oracle_success_and_unknown(self):
        for case in ("success", "unknown"):
            with self.subTest(case=case):
                root = self.tmp / ("oracle-" + case)
                self.assertEqual(run(root, case).returncode, 0)
                first, second = child_id("first"), child_id("second")
                one = (root / "tasks" / first / "call.json").read_bytes()
                digest = __import__("hashlib").sha256(one).hexdigest()
                self.assertNotEqual(first, second)
                if case == "success":
                    two = (root / "tasks" / second / "call.json").read_bytes()
                    self.assertEqual(one, two)
                for ident in ((first, second) if case == "success" else (first,)):
                    if (root / "tasks" / ident / "task.json").exists():
                        self.assertEqual(json.loads((root / "tasks" / ident / "task.json").read_text())["call_ref"]["hash"], digest)
                pe = events(root, ROOT_ID)
                if case == "success":
                    self.assertEqual([x["type"] for x in pe], ["accepted", "child_planned", "child_linked", "child_observed", "child_planned", "child_linked", "child_observed", "receipt_committed"])
                    self.assertTrue((root / "tasks" / second / "events.jsonl").exists())
                else:
                    self.assertEqual([x["type"] for x in pe], ["accepted", "child_planned", "child_linked", "repair_required"])
                    self.assertFalse((root / "tasks" / second).exists())
                    self.assertNotIn("receipt_committed", [x["type"] for x in events(root, first)])

    def test_stale_sidecar_removed_after_healthy_report(self):
        root = self.completed("sidecar")
        sidecar = Path(str(root) + ".corrupt-report.json")
        sidecar.write_text('{"phase":"corrupt"}')
        self.assertEqual(run(root).returncode, 0)
        self.assertFalse(sidecar.exists())

    def test_directory_and_fifo_at_expected_file_are_structured_corruption(self):
        for kind in ("directory", "fifo"):
            with self.subTest(kind=kind):
                root = self.completed("native-" + kind)
                call = root / "tasks" / ROOT_ID / "call.json"
                call.unlink()
                if kind == "directory":
                    call.mkdir()
                else:
                    os.mkfifo(call)
                self.corrupt(root)

    def test_fifo_receipt_is_rejected_before_read_or_block(self):
        root = self.completed("receipt-fifo")
        child = report(root)["task_tree"]["children"][0]
        path = root / "tasks" / child["task_id"] / "payload" / (child["receipt_hash"] + ".receipt.json")
        path.unlink(); os.mkfifo(path)
        got = run(root, timeout=2.0)
        self.assertEqual(got.returncode, 2, got.stderr)
        self.assertEqual(report(root)["phase"], "corrupt")

    def test_corrupt_parent_never_truncates_child_torn_tail(self):
        root = self.tmp / "parent-bad-child-tail"
        self.assertIn(run(root, fault="linked_before", armed=True).returncode, (0, 97))
        child = child_id("first")
        child_events = root / "tasks" / child / "events.jsonl"
        before = child_events.read_bytes() + b'{"schema"'
        child_events.write_bytes(before)
        self.alter_parent_event(root, lambda e: e["type"] == "child_planned",
                                lambda e: e.__setitem__("call_hash", "b" * 64))
        got = run(root, timeout=2.0)
        self.assertEqual(got.returncode, 2, got.stderr)
        self.assertEqual(child_events.read_bytes(), before)

    def test_existing_lexical_root_ancestor_symlink_is_rejected(self):
        real = self.tmp / "real-ancestor"; real.mkdir()
        linked = self.tmp / "linked-ancestor"; os.symlink(real, linked)
        got = run(linked / "child")
        self.assertEqual(got.returncode, 2, got.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
