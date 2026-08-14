from __future__ import annotations

import fcntl
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from p1a2_model import ROOT_FAILPOINTS, Runtime
from p1a2_store import AcceptanceRejected, Corruption, MAX_TREE_BYTES, canonical_json, make_ref
from test_support import CLI, ROOT_ID, file_tree, root_call, run_cli, write_call
from test_vectors import ACCEPTED_LINE, ROOT_CALL_BYTES, ROOT_LINKED_LINE, ROOT_PLANNED_LINE, TASK_BYTES


class Phase1AcceptRecover(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="aos-p1a2-phase1-", dir="/tmp"))
        self.store = self.tmp / "store"
        self.call_file = self.tmp / "root-call.json"
        write_call(self.call_file, root_call())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def accept(self, **kwargs):
        return run_cli(self.store, "accept", call_file=self.call_file, **kwargs)

    def recover(self, **kwargs):
        return run_cli(self.store, "recover", **kwargs)

    def report(self, result: subprocess.CompletedProcess[str]) -> dict:
        return json.loads(result.stdout)

    def make_orphan_store(self, name: str) -> tuple[Path, Path, Path]:
        store = self.tmp / name
        tasks = store / "tasks"
        orphan = tasks / "orphan"
        orphan.mkdir(parents=True)
        (store / "store.lock").write_bytes(b"")
        opaque = orphan / "opaque.bin"
        opaque.write_bytes(b"KEEP")
        return store, orphan, opaque

    def assert_phase1_gap(self, store: Path, point: str) -> None:
        roots = store / "roots.jsonl"
        folder = store / "tasks" / ROOT_ID
        roots_count = roots.read_bytes().count(b"\n") if roots.exists() else 0
        events = folder / "events.jsonl"
        event_count = events.read_bytes().count(b"\n") if events.exists() else 0
        expected = {
            "root_call_before": (0, False, False, 0),
            "root_call_after": (0, True, False, 0),
            "root_planned_before": (0, True, False, 0),
            "root_planned_after": (1, True, False, 0),
            "root_task_before": (1, True, False, 0),
            "root_task_after": (1, True, True, 0),
            "root_accepted_before": (1, True, True, 0),
            "root_accepted_after": (1, True, True, 1),
            "root_linked_before": (1, True, True, 1),
            "root_linked_after": (2, True, True, 1),
        }[point]
        self.assertEqual((roots_count, (folder / "call.json").exists(),
                          (folder / "task.json").exists(), event_count), expected)
        self.assertFalse((folder / "attempts").exists())
        self.assertFalse((folder / "payload").exists())

    def test_happy_accept_and_repeated_recover_are_byte_stable(self):
        result = self.accept()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.report(result)["roots"][0]["state"], "linked")
        before = file_tree(self.store)
        for _ in range(5):
            got = self.recover()
            self.assertEqual(got.returncode, 0, got.stderr)
            self.assertEqual(file_tree(self.store), before)

    def test_real_writer_matches_checked_in_golden_bytes(self):
        self.assertEqual(self.accept().returncode, 0)
        folder = self.store / "tasks" / ROOT_ID
        self.assertEqual((folder / "call.json").read_bytes(), ROOT_CALL_BYTES)
        self.assertEqual((folder / "task.json").read_bytes(), TASK_BYTES)
        self.assertEqual((folder / "events.jsonl").read_bytes(), ACCEPTED_LINE)
        self.assertEqual((self.store / "roots.jsonl").read_bytes(),
                         ROOT_PLANNED_LINE + ROOT_LINKED_LINE)

    def test_failpoint_matrix_exact_97_marker_and_recovery(self):
        pre_plan = {"root_call_before", "root_call_after", "root_planned_before"}
        for point in ROOT_FAILPOINTS:
            with self.subTest(point=point):
                store = self.tmp / ("store-" + point)
                first = run_cli(store, "accept", call_file=self.call_file, failpoint=point)
                self.assertEqual(first.returncode, 97, first.stderr)
                marker = store.with_name(store.name + ".failpoint-used")
                self.assertEqual(marker.read_bytes(), (point + "\n").encode())
                self.assert_phase1_gap(store, point)
                root_events_before = (store / "roots.jsonl").read_bytes().count(b"\n") if (store / "roots.jsonl").exists() else 0
                self.assertFalse((store / "tasks" / ROOT_ID / "attempts").exists())
                if point in pre_plan:
                    empty = run_cli(store, "recover", failpoint=point)
                    self.assertEqual(empty.returncode, 0, empty.stderr)
                    self.assertEqual(json.loads(empty.stdout)["roots"], [])
                    resumed = run_cli(store, "accept", call_file=self.call_file, failpoint=point)
                else:
                    resumed = run_cli(store, "recover", failpoint=point)
                self.assertEqual(resumed.returncode, 0, resumed.stderr)
                self.assertEqual(json.loads(resumed.stdout)["roots"][0]["state"], "linked")
                stable = file_tree(store)
                again = run_cli(store, "recover", failpoint=point)
                self.assertEqual(again.returncode, 0, again.stderr)
                self.assertEqual(file_tree(store), stable)
                self.assertGreaterEqual((store / "roots.jsonl").read_bytes().count(b"\n"), root_events_before)
                self.assertFalse((store / "tasks" / ROOT_ID / "attempts").exists())

    def test_valid_but_unreachable_failpoint_does_not_exit_or_mark(self):
        self.assertEqual(self.accept().returncode, 0)
        got = self.recover(failpoint="root_call_before")
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertFalse(self.store.with_name(self.store.name + ".failpoint-used").exists())

    def test_wrong_or_unreachable_failpoint_is_rejected_without_marker(self):
        cmd = [sys.executable, str(CLI), "--store", str(self.store), "--failpoint", "not-a-point", "recover"]
        got = subprocess.run(cmd, text=True, capture_output=True, check=False,
                             env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
        self.assertNotEqual(got.returncode, 97)
        self.assertFalse(self.store.with_name(self.store.name + ".failpoint-used").exists())

    def test_same_id_call_only_staging_is_replaced_only_by_explicit_accept(self):
        first = run_cli(self.store, "accept", call_file=self.call_file, failpoint="root_call_after")
        self.assertEqual(first.returncode, 97)
        staged = self.store / "tasks" / ROOT_ID / "call.json"
        old = staged.read_bytes()
        changed = root_call(definition="/opt/aos-fixture/new-process")
        write_call(self.call_file, changed)
        recovered = self.recover()
        self.assertEqual(recovered.returncode, 0, recovered.stderr)
        self.assertEqual(json.loads(recovered.stdout)["roots"], [])
        self.assertEqual(staged.read_bytes(), old)
        accepted = self.accept()
        self.assertEqual(accepted.returncode, 0, accepted.stderr)
        self.assertNotEqual(staged.read_bytes(), old)

    def test_allowed_call_temp_is_removed_by_explicit_reaccept(self):
        self.store.mkdir(); (self.store / "tasks").mkdir(); (self.store / "store.lock").write_bytes(b"")
        candidate = self.store / "tasks" / ROOT_ID; candidate.mkdir()
        (candidate / "call.json").write_bytes(canonical_json(root_call()))
        temporary = candidate / ".call.json.ABCDEF"
        temporary.write_bytes(b"interrupted staging")
        got = self.accept()
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertFalse(temporary.exists())

    def test_opaque_but_bounded_preplan_call_is_discardable_staging(self):
        self.store.mkdir(); (self.store / "tasks").mkdir(); (self.store / "store.lock").write_bytes(b"")
        candidate = self.store / "tasks" / ROOT_ID; candidate.mkdir()
        (candidate / "call.json").write_bytes(b"not committed JSON")
        got = self.accept()
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual((candidate / "call.json").read_bytes(), ROOT_CALL_BYTES)

    def test_candidate_anomaly_and_invalid_resolver_preserve_bytes(self):
        self.store.mkdir(); (self.store / "tasks").mkdir(); (self.store / "store.lock").write_bytes(b"")
        candidate = self.store / "tasks" / ROOT_ID; candidate.mkdir()
        poison = candidate / "task.json"; poison.write_bytes(b"do-not-touch")
        before = file_tree(self.store)
        got = self.accept()
        self.assertEqual(got.returncode, 2)
        self.assertEqual(file_tree(self.store), before)
        poison.unlink()
        (candidate / "call.json").write_bytes(canonical_json(root_call()))
        bad = root_call(); bad["children"][0]["recipe"]["environment_policy"] = {"kind": "other"}
        write_call(self.call_file, bad)
        before = file_tree(self.store)
        got = self.accept()
        self.assertEqual(got.returncode, 2)
        self.assertEqual(file_tree(self.store), before)

    def test_already_planned_rejects_without_finishing_gap(self):
        first = run_cli(self.store, "accept", call_file=self.call_file, failpoint="root_planned_after")
        self.assertEqual(first.returncode, 97)
        before = file_tree(self.store)
        got = self.accept()
        self.assertEqual(got.returncode, 2)
        self.assertEqual(file_tree(self.store), before)
        recovered = self.recover()
        self.assertEqual(recovered.returncode, 0, recovered.stderr)

    def test_already_planned_does_not_open_poison_resolver_input(self):
        self.assertEqual(self.accept().returncode, 0)
        poison = self.tmp / "resolver-fifo"
        os.mkfifo(poison)
        got = run_cli(self.store, "accept", call_file=poison, timeout=2.0)
        self.assertEqual(got.returncode, 2, got.stderr)

    def test_in_process_poison_resolver_is_not_called_after_plan(self):
        runtime = Runtime(self.store)
        self.assertTrue(runtime.accept_fixture_root(ROOT_ID, lambda: root_call()).root.linked)
        count = 0
        def poison():
            nonlocal count
            count += 1
            raise AssertionError("must not run")
        before = file_tree(self.store)
        with self.assertRaises(AcceptanceRejected):
            runtime.accept_fixture_root(ROOT_ID, poison)
        self.assertEqual(count, 0)
        self.assertEqual(file_tree(self.store), before)

    def test_mutating_resolver_cannot_cross_authority_or_lock_barrier(self):
        runtime = Runtime(self.store)
        def replace_lock():
            lock = self.store / "store.lock"
            lock.unlink(); lock.write_bytes(b"replacement inode")
            return root_call()
        with self.assertRaises(Corruption):
            runtime.accept_fixture_root(ROOT_ID, replace_lock)
        self.assertFalse((self.store / "roots.jsonl").exists())
        self.assertFalse((self.store / "tasks" / ROOT_ID).exists())

    def test_mutating_then_throwing_resolver_still_checks_lock_and_candidate(self):
        runtime = Runtime(self.store)
        def replace_lock_then_throw():
            lock = self.store / "store.lock"
            lock.unlink(); lock.write_bytes(b"replacement inode")
            raise RuntimeError("resolver failed after mutation")
        with self.assertRaises(Corruption):
            runtime.accept_fixture_root(ROOT_ID, replace_lock_then_throw)
        self.assertFalse((self.store / "roots.jsonl").exists())

        other = self.tmp / "candidate-mutation"
        runtime = Runtime(other)
        def create_empty_candidate_then_throw():
            (other / "tasks" / ROOT_ID).mkdir()
            raise RuntimeError("resolver failed after candidate mutation")
        with self.assertRaises(Corruption):
            runtime.accept_fixture_root(ROOT_ID, create_empty_candidate_then_throw)
        self.assertFalse((other / "roots.jsonl").exists())

    def test_resolver_cannot_replace_the_tasks_tree_and_hide_an_orphan(self):
        store, _, opaque = self.make_orphan_store("replace-tasks-tree")
        moved_tasks = self.tmp / "tasks-moved-outside-store"
        calls = 0

        def replace_tasks_tree():
            nonlocal calls
            calls += 1
            os.rename(store / "tasks", moved_tasks)
            (store / "tasks").mkdir()
            return root_call()

        with self.assertRaises(Corruption):
            Runtime(store).accept_fixture_root(ROOT_ID, replace_tasks_tree)
        self.assertEqual(calls, 1)
        self.assertEqual((moved_tasks / "orphan" / "opaque.bin").read_bytes(), b"KEEP")
        self.assertFalse((store / "roots.jsonl").exists())
        self.assertFalse((store / "tasks" / ROOT_ID).exists())
        self.assertFalse(opaque.exists())

    def test_resolver_cannot_replace_an_ancestor_with_a_symlink(self):
        parent = self.tmp / "store-parent"
        store = parent / "store"
        orphan = store / "tasks" / "orphan"
        orphan.mkdir(parents=True)
        (store / "store.lock").write_bytes(b"")
        (orphan / "opaque.bin").write_bytes(b"KEEP")
        moved_parent = self.tmp / "store-parent-moved"
        calls = 0

        def replace_ancestor():
            nonlocal calls
            calls += 1
            os.rename(parent, moved_parent)
            parent.symlink_to(moved_parent, target_is_directory=True)
            return root_call()

        with self.assertRaises(Corruption):
            Runtime(store).accept_fixture_root(ROOT_ID, replace_ancestor)
        self.assertEqual(calls, 1)
        self.assertFalse((moved_parent / "store" / "roots.jsonl").exists())
        self.assertFalse((moved_parent / "store" / "tasks" / ROOT_ID).exists())
        with self.assertRaises(Corruption):
            Runtime(store).recover_store()

    def test_resolver_cannot_mutate_an_orphan_file_in_place(self):
        store, _, opaque = self.make_orphan_store("mutate-orphan-file")

        def mutate_orphan():
            opaque.write_bytes(b"MUT8")  # same length; content fingerprint must notice
            return root_call()

        with self.assertRaises(Corruption):
            Runtime(store).accept_fixture_root(ROOT_ID, mutate_orphan)
        self.assertEqual(opaque.read_bytes(), b"MUT8")
        self.assertFalse((store / "roots.jsonl").exists())
        self.assertFalse((store / "tasks" / ROOT_ID).exists())

    def test_resolver_base_exception_still_cannot_skip_tree_guard(self):
        store, _, opaque = self.make_orphan_store("base-exception-mutation")

        def mutate_then_exit():
            opaque.write_bytes(b"MUT8")
            raise SystemExit(9)

        with self.assertRaises(Corruption):
            Runtime(store).accept_fixture_root(ROOT_ID, mutate_then_exit)
        self.assertFalse((store / "roots.jsonl").exists())
        self.assertFalse((store / "tasks" / ROOT_ID).exists())

    def test_resolver_cannot_rename_an_orphan(self):
        store, orphan, _ = self.make_orphan_store("rename-orphan")
        renamed = store / "tasks" / "renamed"

        def rename_orphan():
            os.rename(orphan, renamed)
            return root_call()

        with self.assertRaises(Corruption):
            Runtime(store).accept_fixture_root(ROOT_ID, rename_orphan)
        self.assertTrue(renamed.is_dir())
        self.assertFalse((store / "roots.jsonl").exists())
        self.assertFalse((store / "tasks" / ROOT_ID).exists())

    def test_resolver_guard_does_not_follow_or_open_orphan_symlink_and_fifo(self):
        store, orphan, _ = self.make_orphan_store("special-orphan")
        os.mkfifo(orphan / "pipe")
        (orphan / "link").symlink_to("/proc/self/fd/0")
        projection = Runtime(store).accept_fixture_root(ROOT_ID, lambda: root_call())
        self.assertTrue(projection.root.linked)

    def test_resolver_guard_rejects_oversize_orphan_before_callback(self):
        store, _, opaque = self.make_orphan_store("oversize-orphan")
        with opaque.open("r+b") as stream:
            stream.truncate(MAX_TREE_BYTES + 1)
        calls = 0

        def resolver():
            nonlocal calls
            calls += 1
            return root_call()

        with self.assertRaises(Corruption):
            Runtime(store).accept_fixture_root(ROOT_ID, resolver)
        self.assertEqual(calls, 0)
        self.assertFalse((store / "roots.jsonl").exists())

    def test_root_id_separator_is_rejected_before_resolver_candidate_or_write(self):
        poison = self.tmp / "bad-root-resolver-fifo"
        os.mkfifo(poison)
        got = run_cli(self.store, "accept", root_id="bad--root",
                      call_file=poison, timeout=2.0)
        self.assertEqual(got.returncode, 2, got.stderr)
        self.assertTrue(got.stderr.startswith("accept rejected: "), got.stderr)
        self.assertFalse(self.store.exists())

    def test_cli_reports_committed_corruption_separately(self):
        self.store.mkdir(); (self.store / "tasks").mkdir()
        (self.store / "store.lock").write_bytes(b"")
        (self.store / "roots.jsonl").write_bytes(b'{"type":"evil","v":1}\n')
        got = self.recover()
        self.assertEqual(got.returncode, 2, got.stderr)
        self.assertTrue(got.stderr.startswith("corruption: "), got.stderr)

    def test_existing_nonempty_store_without_lock_is_not_bootstrapped(self):
        self.store.mkdir(); (self.store / "tasks").mkdir()
        before = file_tree(self.store)
        got = self.accept()
        self.assertEqual(got.returncode, 2)
        self.assertEqual(file_tree(self.store), before)
        self.assertFalse((self.store / "store.lock").exists())

    def test_other_orphan_bytes_are_untouched(self):
        result = self.accept()
        self.assertEqual(result.returncode, 0, result.stderr)
        orphan = self.store / "tasks" / "orphan"; orphan.mkdir()
        (orphan / "opaque.bin").write_bytes(b"leave me")
        before = file_tree(orphan)
        got = self.recover()
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual(file_tree(orphan), before)

    def test_torn_roots_and_task_logs_repair_only_after_whole_projection(self):
        self.assertEqual(self.accept().returncode, 0)
        roots = self.store / "roots.jsonl"
        events = self.store / "tasks" / ROOT_ID / "events.jsonl"
        roots_clean, events_clean = roots.read_bytes(), events.read_bytes()
        roots.write_bytes(roots_clean + b'{"v":1')
        events.write_bytes(events_clean + b'{"v":1')
        got = self.recover()
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual(roots.read_bytes(), roots_clean)
        self.assertEqual(events.read_bytes(), events_clean)
        # A committed contradiction prevents either safe-looking tail repair.
        roots.write_bytes(roots_clean + b'{"v":1')
        events.write_bytes(events_clean + b'{"type":"evil","v":1}\n' + b'{"v":1')
        before_roots, before_events = roots.read_bytes(), events.read_bytes()
        bad = self.recover()
        self.assertEqual(bad.returncode, 2)
        self.assertEqual(roots.read_bytes(), before_roots)
        self.assertEqual(events.read_bytes(), before_events)

    def test_torn_accepted_line_recovers_from_committed_task_gap(self):
        first = run_cli(self.store, "accept", call_file=self.call_file, failpoint="root_task_after")
        self.assertEqual(first.returncode, 97)
        events = self.store / "tasks" / ROOT_ID / "events.jsonl"
        events.write_bytes(b'{"type":"accepted"')
        got = run_cli(self.store, "recover", failpoint="root_task_after")
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual(events.read_bytes(), canonical_json({"v": 1, "type": "accepted"}))

    def test_unlinked_accepted_without_task_materializes_then_links(self):
        first = run_cli(self.store, "accept", call_file=self.call_file, failpoint="root_planned_after")
        self.assertEqual(first.returncode, 97)
        folder = self.store / "tasks" / ROOT_ID
        (folder / "events.jsonl").write_bytes(canonical_json({"v": 1, "type": "accepted"}))
        self.assertFalse((folder / "task.json").exists())
        got = run_cli(self.store, "recover", failpoint="root_planned_after")
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual((folder / "task.json").read_bytes(), TASK_BYTES)
        self.assertEqual(json.loads(got.stdout)["roots"][0]["state"], "linked")

    def test_only_torn_root_plan_is_discarded_without_scanning_staging(self):
        self.store.mkdir(); (self.store / "tasks").mkdir(); (self.store / "store.lock").write_bytes(b"")
        candidate = self.store / "tasks" / ROOT_ID; candidate.mkdir()
        opaque = b"opaque pre-plan staging"
        (candidate / "call.json").write_bytes(opaque)
        roots = self.store / "roots.jsonl"; roots.write_bytes(b'{"root_id":"root"')
        got = self.recover()
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual(json.loads(got.stdout)["roots"], [])
        self.assertEqual(roots.read_bytes(), b"")
        self.assertEqual((candidate / "call.json").read_bytes(), opaque)

    def test_legal_tail_is_not_repaired_when_reachable_call_is_corrupt(self):
        self.assertEqual(self.accept().returncode, 0)
        roots = self.store / "roots.jsonl"; roots.write_bytes(roots.read_bytes() + b'{"torn"')
        call = self.store / "tasks" / ROOT_ID / "call.json"
        call.write_bytes(canonical_json(root_call(definition="/tampered")))
        before = roots.read_bytes()
        got = self.recover()
        self.assertEqual(got.returncode, 2)
        self.assertEqual(roots.read_bytes(), before)

    def test_registry_reverse_duplicate_mismatch_and_linked_gap_corrupt(self):
        variants = ("linked_first", "duplicate", "mismatch", "linked_missing_task",
                    "linked_missing_accepted", "task_ref", "call_bytes")
        for variant in variants:
            with self.subTest(variant=variant):
                store = self.tmp / ("bad-" + variant)
                self.assertEqual(run_cli(store, "accept", call_file=self.call_file).returncode, 0)
                roots = store / "roots.jsonl"
                lines = [json.loads(x) for x in roots.read_text().splitlines()]
                if variant == "linked_first": lines = [lines[1], lines[0]]
                elif variant == "duplicate": lines.append(lines[1])
                elif variant == "mismatch": lines[1]["call_ref"]["sha256"] = "b" * 64
                elif variant == "linked_missing_task":
                    (store / "tasks" / ROOT_ID / "task.json").unlink()
                elif variant == "linked_missing_accepted":
                    (store / "tasks" / ROOT_ID / "events.jsonl").unlink()
                elif variant == "task_ref":
                    task = store / "tasks" / ROOT_ID / "task.json"
                    obj = json.loads(task.read_text()); obj["call_ref"]["sha256"] = "b" * 64
                    task.write_bytes(canonical_json(obj))
                else:
                    (store / "tasks" / ROOT_ID / "call.json").write_bytes(canonical_json(root_call(definition="/changed")))
                if variant not in ("linked_missing_task", "linked_missing_accepted", "task_ref", "call_bytes"):
                    roots.write_bytes(b"".join(canonical_json(x) for x in lines))
                before = file_tree(store)
                got = run_cli(store, "recover")
                self.assertEqual(got.returncode, 2)
                self.assertEqual(file_tree(store), before)

    def test_noncanonical_or_malformed_committed_lines_never_repair(self):
        variants = (
            b'{"v":1,"type":"root_planned"}\n',
            b'{"v":1,"v":1,"type":"root_planned"}\n',
            b'{"v":NaN}\n',
            b'{ "v": 1 }\n',
        )
        for index, line in enumerate(variants):
            with self.subTest(index=index):
                store = self.tmp / ("line-" + str(index)); store.mkdir()
                (store / "tasks").mkdir(); (store / "store.lock").write_bytes(b"")
                roots = store / "roots.jsonl"; roots.write_bytes(line + b'{"torn"')
                before = roots.read_bytes()
                got = run_cli(store, "recover")
                self.assertEqual(got.returncode, 2)
                self.assertEqual(roots.read_bytes(), before)

    def test_fifo_symlink_directory_and_unknown_candidate_entries_fail_fast(self):
        for kind in ("fifo", "symlink", "directory", "unknown"):
            with self.subTest(kind=kind):
                store = self.tmp / ("native-" + kind); store.mkdir()
                (store / "tasks").mkdir(); (store / "store.lock").write_bytes(b"")
                candidate = store / "tasks" / ROOT_ID; candidate.mkdir()
                target = candidate / "call.json"
                if kind == "fifo": os.mkfifo(target)
                elif kind == "symlink": target.symlink_to(self.call_file)
                elif kind == "directory": target.mkdir()
                else: (candidate / "unknown.bin").write_bytes(b"x")
                before = file_tree(store)
                got = run_cli(store, "accept", call_file=self.call_file, timeout=2.0)
                self.assertEqual(got.returncode, 2, got.stderr)
                self.assertEqual(file_tree(store), before)

    def test_fifo_and_symlink_authority_files_never_block_or_write_through(self):
        for kind in ("roots_fifo", "lock_symlink"):
            with self.subTest(kind=kind):
                store = self.tmp / kind; store.mkdir(); (store / "tasks").mkdir()
                if kind == "roots_fifo":
                    (store / "store.lock").write_bytes(b""); os.mkfifo(store / "roots.jsonl")
                else:
                    outside = self.tmp / (kind + "-outside"); outside.write_bytes(b"unchanged")
                    (store / "store.lock").symlink_to(outside)
                before = file_tree(store)
                got = run_cli(store, "recover", timeout=2.0)
                self.assertEqual(got.returncode, 2, got.stderr)
                self.assertEqual(file_tree(store), before)

    def test_reachable_temp_must_be_bounded_regular_file(self):
        self.assertEqual(self.accept().returncode, 0)
        temp = self.store / "tasks" / ROOT_ID / ".task.json.ABCDEF"
        os.mkfifo(temp)
        before = file_tree(self.store)
        got = self.recover(timeout=2.0)
        self.assertEqual(got.returncode, 2, got.stderr)
        self.assertEqual(file_tree(self.store), before)

    def test_directory_entry_limit_is_enforced_before_projection(self):
        self.assertEqual(self.accept().returncode, 0)
        for index in range(64):
            (self.store / "tasks" / ("x" + str(index))).mkdir()
        before = file_tree(self.store)
        got = self.recover()
        self.assertEqual(got.returncode, 2)
        self.assertEqual(file_tree(self.store), before)

    def test_full_tasks_directory_rejects_before_resolver_or_candidate_write(self):
        self.store.mkdir(); tasks = self.store / "tasks"; tasks.mkdir()
        (self.store / "store.lock").write_bytes(b"")
        for index in range(64):
            (tasks / f"orphan{index}").mkdir()
        calls = 0

        def resolver():
            nonlocal calls
            calls += 1
            return root_call()

        before = file_tree(self.store)
        with self.assertRaises(AcceptanceRejected):
            Runtime(self.store).accept_fixture_root(ROOT_ID, resolver)
        self.assertEqual(calls, 0)
        self.assertEqual(file_tree(self.store), before)
        self.assertFalse((self.store / "roots.jsonl").exists())
        self.assertFalse((tasks / ROOT_ID).exists())

    def test_planned_folder_cleans_crash_temps_before_remaining_transitions(self):
        cases = (("before-task", "root_planned_after", 63),
                 ("before-events", "root_task_after", 62))
        for name, failpoint, temp_count in cases:
            with self.subTest(name=name):
                store = self.tmp / name
                first = run_cli(store, "accept", call_file=self.call_file,
                                failpoint=failpoint)
                self.assertEqual(first.returncode, 97, first.stderr)
                folder = store / "tasks" / ROOT_ID
                for index in range(temp_count):
                    (folder / f".task.json.T{index:05d}").write_bytes(b"stale")
                got = run_cli(store, "recover")
                self.assertEqual(got.returncode, 0, got.stderr)
                self.assertEqual(self.report(got)["roots"][0]["state"], "linked")
                self.assertFalse(any(item.name.startswith(".") for item in folder.iterdir()))
                self.assertEqual((folder / "events.jsonl").read_bytes(), ACCEPTED_LINE)
                stable = file_tree(store)
                again = run_cli(store, "recover")
                self.assertEqual(again.returncode, 0, again.stderr)
                self.assertEqual(file_tree(store), stable)

    def test_exclusive_lock_serializes_second_writer(self):
        self.assertEqual(self.accept().returncode, 0)
        lock = os.open(self.store / "store.lock", os.O_RDWR)
        try:
            fcntl.flock(lock, fcntl.LOCK_EX)
            cmd = [sys.executable, str(CLI), "--store", str(self.store), "recover"]
            with self.assertRaises(subprocess.TimeoutExpired):
                subprocess.run(cmd, text=True, capture_output=True, timeout=0.25,
                               env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN); os.close(lock)
        self.assertEqual(self.recover().returncode, 0)

    def test_concurrent_accept_on_new_store_has_one_winner(self):
        cmd = [sys.executable, str(CLI), "--store", str(self.store), "accept",
               "--root-id", ROOT_ID, "--call-file", str(self.call_file)]
        env = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}
        one = subprocess.Popen(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        two = subprocess.Popen(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        out1, err1 = one.communicate(timeout=10); out2, err2 = two.communicate(timeout=10)
        self.assertEqual(sorted((one.returncode, two.returncode)), [0, 2], (out1, err1, out2, err2))
        self.assertEqual(self.recover().returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
