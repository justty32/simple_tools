from __future__ import annotations

import shutil
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from p1a2_composite_fake import (
    ALL_FAKE_FAILPOINTS, COMPOSITE_CUTS, CapacityUnavailable,
    CompositeFakeRuntime, StagingCollision,
)
from p1a2_model import ROOT_FAILPOINTS
from p1a2_store import AcceptanceRejected, Corruption
from test_composite_fake_vectors import (
    FAKE_CALL_BYTES, FIRST_RECEIPT_BYTES, ROOT_FAKE_BYTES, ROOT_RECEIPT_BYTES,
    SECOND_RECEIPT_BYTES,
)
from test_support import file_tree, root_fake_call, write_call

ROOTS_BYTES = b'{"call_ref":{"sha256":"2c054a622a87203d3c57004af4f26f7dc5016e2ccc2f0f5c19f6a864ce5f1db6","size":561},"root_id":"root","type":"root_planned","v":1}\n{"call_ref":{"sha256":"2c054a622a87203d3c57004af4f26f7dc5016e2ccc2f0f5c19f6a864ce5f1db6","size":561},"root_id":"root","type":"root_linked","v":1}\n'
ROOT_TASK_BYTES = b'{"call_ref":{"sha256":"2c054a622a87203d3c57004af4f26f7dc5016e2ccc2f0f5c19f6a864ce5f1db6","size":561},"task_id":"root","v":1}\n'
FIRST_TASK_BYTES = b'{"call_ref":{"sha256":"3bb33a69281456e755654ff03a563fa4b08366e8fce0a61ce849e793ce26864d","size":160},"task_id":"root--first","v":1}\n'
SECOND_TASK_BYTES = b'{"call_ref":{"sha256":"3bb33a69281456e755654ff03a563fa4b08366e8fce0a61ce849e793ce26864d","size":160},"task_id":"root--second","v":1}\n'
FIRST_EVENTS_BYTES = b'{"type":"accepted","v":1}\n{"receipt_ref":{"sha256":"7aa45814efdc3a44b9e9897d7831a6901d188fcdd26214f86af4edb377230205","size":445},"type":"receipt_committed","v":1}\n'
SECOND_EVENTS_BYTES = b'{"type":"accepted","v":1}\n{"receipt_ref":{"sha256":"ad21b758d8048e08ad6521f186968046bcfc7a5a4bd865f43889976d163ad12b","size":447},"type":"receipt_committed","v":1}\n'
ROOT_EVENTS_BYTES = b'{"type":"accepted","v":1}\n{"call_ref":{"sha256":"3bb33a69281456e755654ff03a563fa4b08366e8fce0a61ce849e793ce26864d","size":160},"slot":"first","task_id":"root--first","type":"child_planned","v":1}\n{"call_ref":{"sha256":"3bb33a69281456e755654ff03a563fa4b08366e8fce0a61ce849e793ce26864d","size":160},"slot":"first","task_id":"root--first","type":"child_linked","v":1}\n{"receipt_ref":{"sha256":"7aa45814efdc3a44b9e9897d7831a6901d188fcdd26214f86af4edb377230205","size":445},"slot":"first","task_id":"root--first","type":"child_observed","v":1}\n{"call_ref":{"sha256":"3bb33a69281456e755654ff03a563fa4b08366e8fce0a61ce849e793ce26864d","size":160},"slot":"second","task_id":"root--second","type":"child_planned","v":1}\n{"call_ref":{"sha256":"3bb33a69281456e755654ff03a563fa4b08366e8fce0a61ce849e793ce26864d","size":160},"slot":"second","task_id":"root--second","type":"child_linked","v":1}\n{"receipt_ref":{"sha256":"ad21b758d8048e08ad6521f186968046bcfc7a5a4bd865f43889976d163ad12b","size":447},"slot":"second","task_id":"root--second","type":"child_observed","v":1}\n{"receipt_ref":{"sha256":"f6385567cd5f1b6543cf76951e884a7ef097a4c16b01c1547134dd178c2f6599","size":506},"type":"receipt_committed","v":1}\n'

EXPECTED_AUTHORITY = {
    "roots.jsonl": ROOTS_BYTES,
    "tasks/root/call.json": ROOT_FAKE_BYTES,
    "tasks/root/task.json": ROOT_TASK_BYTES,
    "tasks/root/events.jsonl": ROOT_EVENTS_BYTES,
    "tasks/root/payload/f6385567cd5f1b6543cf76951e884a7ef097a4c16b01c1547134dd178c2f6599.receipt.json": ROOT_RECEIPT_BYTES,
    "tasks/root--first/call.json": FAKE_CALL_BYTES,
    "tasks/root--first/task.json": FIRST_TASK_BYTES,
    "tasks/root--first/events.jsonl": FIRST_EVENTS_BYTES,
    "tasks/root--first/payload/7aa45814efdc3a44b9e9897d7831a6901d188fcdd26214f86af4edb377230205.receipt.json": FIRST_RECEIPT_BYTES,
    "tasks/root--second/call.json": FAKE_CALL_BYTES,
    "tasks/root--second/task.json": SECOND_TASK_BYTES,
    "tasks/root--second/events.jsonl": SECOND_EVENTS_BYTES,
    "tasks/root--second/payload/ad21b758d8048e08ad6521f186968046bcfc7a5a4bd865f43889976d163ad12b.receipt.json": SECOND_RECEIPT_BYTES,
}


class CompositeFakeCleanTree(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="aos-p1a2-fake-", dir="/tmp"))
        self.store = self.tmp / "store"

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def authority(self) -> dict[str, bytes]:
        result = {}
        for path in sorted(self.store.rglob("*")):
            if path.is_file() and path.name != "store.lock":
                result[path.relative_to(self.store).as_posix()] = path.read_bytes()
        return result

    def worker(self, action: str, failpoint: str) -> subprocess.CompletedProcess[str]:
        source = (
            "import sys; from p1a2_composite_fake import CompositeFakeRuntime; "
            "from test_support import root_fake_call; "
            "r=CompositeFakeRuntime(sys.argv[1],failpoint=sys.argv[3]); "
            "r.accept_fixture_root('root',root_fake_call) if sys.argv[2]=='accept' "
            "else r.recover_store()"
        )
        got = subprocess.run([sys.executable, "-c", source, str(self.store),
                              action, failpoint], text=True, capture_output=True,
                             check=False, timeout=15,
                             env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
        if got.returncode == 97:
            self.store.with_name(self.store.name + ".failpoint-used").write_bytes(
                (failpoint + "\n").encode("ascii"))
        return got

    def input_worker(self, call_file: Path, failpoint: str) -> subprocess.CompletedProcess[str]:
        source = (
            "import sys; from p1a2_composite_fake import CompositeFakeRuntime; "
            "from p1a2_store import MAX_JSON,read_regular,strict_json; "
            "f=sys.argv[2]; r=CompositeFakeRuntime(sys.argv[1],failpoint=sys.argv[3]); "
            "r.accept_fixture_root('root',lambda:strict_json("
            "read_regular(__import__('pathlib').Path(f),MAX_JSON,'input'),'input'))"
        )
        got = subprocess.run([sys.executable, "-c", source, str(self.store),
                              str(call_file), failpoint], text=True,
                             capture_output=True, check=False, timeout=15,
                             env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
        if got.returncode == 97:
            self.store.with_name(self.store.name + ".failpoint-used").write_bytes(
                (failpoint + "\n").encode("ascii"))
        return got

    def test_clean_tree_matches_all_checked_in_authority_bytes_and_is_stable(self):
        projection = CompositeFakeRuntime(self.store).accept_fixture_root(
            "root", root_fake_call)
        self.assertEqual(projection.report()["roots"][0]["state"], "receipt_committed")
        self.assertTrue(projection.root.linked)
        self.assertTrue(projection.root.first.planned and projection.root.first.linked)
        self.assertTrue(projection.root.second.planned and projection.root.second.linked)
        self.assertEqual(self.authority(), EXPECTED_AUTHORITY)
        self.assertEqual(set(path.name for path in (self.store / "tasks").iterdir()),
                         {"root", "root--first", "root--second"})
        before = file_tree(self.store)
        for _ in range(5):
            again = CompositeFakeRuntime(self.store).recover_store()
            self.assertEqual(again.report()["roots"][0]["state"], "receipt_committed")
            self.assertEqual(file_tree(self.store), before)

    def test_oracle_mismatch_stops_after_observing_first(self):
        projection = CompositeFakeRuntime(self.store).accept_fixture_root(
            "root", lambda: root_fake_call(oracle_stdout=b"different\n"))
        self.assertEqual(projection.report()["roots"][0]["state"],
                         "waiting_for_parent_policy")
        self.assertFalse((self.store / "tasks" / "root--second").exists())
        self.assertFalse((self.store / "tasks" / "root" / "payload").exists())
        before = file_tree(self.store)
        for _ in range(5):
            CompositeFakeRuntime(self.store).recover_store()
            self.assertEqual(file_tree(self.store), before)

    def test_all_23_composite_cuts_exit_97_and_recover_to_same_bytes(self):
        self.assertEqual(len(COMPOSITE_CUTS), 23)
        self.assertEqual(len(ALL_FAKE_FAILPOINTS), 33)
        for point in COMPOSITE_CUTS:
            with self.subTest(point=point):
                self.store = self.tmp / ("cut-" + point)
                first = self.worker("accept", point)
                self.assertEqual(first.returncode, 97, first.stderr)
                marker = self.store.with_name(self.store.name + ".failpoint-used")
                self.assertEqual(marker.read_bytes(), (point + "\n").encode("ascii"))
                resumed = self.worker("recover", point)
                self.assertEqual(resumed.returncode, 0, resumed.stderr)
                self.assertEqual(self.authority(), EXPECTED_AUTHORITY)
                stable = file_tree(self.store)
                for _ in range(5):
                    again = self.worker("recover", point)
                    self.assertEqual(again.returncode, 0, again.stderr)
                    self.assertEqual(file_tree(self.store), stable)

    def test_existing_10_root_cuts_chain_into_composite_recovery(self):
        pre_plan = {"root_call_before", "root_call_after", "root_planned_before"}
        for point in ROOT_FAILPOINTS:
            with self.subTest(point=point):
                self.store = self.tmp / ("root-cut-" + point)
                first = self.worker("accept", point)
                self.assertEqual(first.returncode, 97, first.stderr)
                if point in pre_plan:
                    empty = self.worker("recover", point)
                    self.assertEqual(empty.returncode, 0, empty.stderr)
                    resumed = self.worker("accept", point)
                else:
                    resumed = self.worker("recover", point)
                self.assertEqual(resumed.returncode, 0, resumed.stderr)
                self.assertEqual(self.authority(), EXPECTED_AUTHORITY)

    def test_capacity_61_succeeds_and_62_rejects_before_authority(self):
        for count in (61, 62):
            with self.subTest(count=count):
                self.store = self.tmp / ("capacity-" + str(count))
                tasks = self.store / "tasks"; tasks.mkdir(parents=True)
                (self.store / "store.lock").write_bytes(b"")
                for index in range(count):
                    (tasks / f"orphan{index}").mkdir()
                before = file_tree(self.store)
                if count == 61:
                    projection = CompositeFakeRuntime(self.store).accept_fixture_root(
                        "root", root_fake_call)
                    self.assertEqual(projection.report()["roots"][0]["state"],
                                     "receipt_committed")
                    self.assertEqual(len(list(tasks.iterdir())), 64)
                else:
                    calls = 0
                    def resolver():
                        nonlocal calls
                        calls += 1
                        return root_fake_call()
                    with self.assertRaises(CapacityUnavailable):
                        CompositeFakeRuntime(self.store).accept_fixture_root(
                            "root", resolver)
                    self.assertEqual(calls, 1)
                    self.assertEqual(file_tree(self.store), before)
                    self.assertFalse((self.store / "roots.jsonl").exists())

    def test_recovery_is_closed_over_frozen_call_after_input_delete_or_fifo(self):
        for mode in ("delete", "fifo"):
            with self.subTest(mode=mode):
                self.store = self.tmp / ("closed-" + mode)
                call_file = self.tmp / ("input-" + mode + ".json")
                write_call(call_file, root_fake_call())
                first = self.input_worker(call_file, "root_planned_after")
                self.assertEqual(first.returncode, 97, first.stderr)
                call_file.unlink()
                if mode == "fifo":
                    os.mkfifo(call_file)
                calls = 0
                def poison():
                    nonlocal calls
                    calls += 1
                    raise AssertionError("planned root must reject before resolver")
                with self.assertRaises(AcceptanceRejected):
                    CompositeFakeRuntime(self.store).accept_fixture_root("root", poison)
                self.assertEqual(calls, 0)
                resumed = self.worker("recover", "root_planned_after")
                self.assertEqual(resumed.returncode, 0, resumed.stderr)
                self.assertEqual(self.authority(), EXPECTED_AUTHORITY)

    def test_committed_order_call_payload_and_role_tamper_fail_closed(self):
        variants = ("child_call", "root_order", "receipt", "missing_task",
                    "attempts", "bool_v")
        for variant in variants:
            with self.subTest(variant=variant):
                self.store = self.tmp / ("tamper-" + variant)
                CompositeFakeRuntime(self.store).accept_fixture_root("root", root_fake_call)
                if variant == "child_call":
                    (self.store / "tasks/root--first/call.json").write_bytes(b"tamper\n")
                elif variant == "root_order":
                    events = self.store / "tasks/root/events.jsonl"
                    lines = events.read_bytes().splitlines(keepends=True)
                    lines[1], lines[2] = lines[2], lines[1]
                    events.write_bytes(b"".join(lines))
                elif variant == "receipt":
                    path = next((self.store / "tasks/root--first/payload").iterdir())
                    path.write_bytes(path.read_bytes()[:-2] + b"X\n")
                elif variant == "missing_task":
                    (self.store / "tasks/root--second/task.json").unlink()
                elif variant == "attempts":
                    (self.store / "tasks/root--first/attempts").mkdir()
                else:
                    events = self.store / "tasks/root/events.jsonl"
                    lines = events.read_bytes().splitlines(keepends=True)
                    lines[0] = b'{"type":"accepted","v":true}\n'
                    events.write_bytes(b"".join(lines))
                before = file_tree(self.store)
                with self.assertRaises(Corruption):
                    CompositeFakeRuntime(self.store).recover_store()
                self.assertEqual(file_tree(self.store), before)

    def test_full_child_directory_rejects_new_events_log_before_authority(self):
        self.store = self.tmp / "full-child-events"
        first = self.worker("accept", "first_task_after")
        self.assertEqual(first.returncode, 97, first.stderr)
        folder = self.store / "tasks/root--first"
        for index in range(62):
            (folder / f".task.json.T{index:05d}").write_bytes(b"stale")
        self.assertEqual(len(list(folder.iterdir())), 64)
        before = file_tree(self.store)
        with self.assertRaises(CapacityUnavailable):
            CompositeFakeRuntime(self.store,
                                 failpoint="first_task_after").recover_store()
        self.assertEqual(file_tree(self.store), before)
        self.assertFalse((folder / "events.jsonl").exists())

    def test_staging_collision_blocks_torn_tail_repair_before_any_write(self):
        self.store = self.tmp / "collision-before-tail-repair"
        first = self.worker("accept", "root_linked_after")
        self.assertEqual(first.returncode, 97, first.stderr)
        root_events = self.store / "tasks/root/events.jsonl"
        root_events.write_bytes(root_events.read_bytes() + b'{"torn"')
        child = self.store / "tasks/root--first"; child.mkdir()
        (child / "task.json").write_bytes(b"collision")
        before = file_tree(self.store)
        with self.assertRaises(StagingCollision):
            CompositeFakeRuntime(self.store,
                                 failpoint="root_linked_after").recover_store()
        self.assertEqual(file_tree(self.store), before)


if __name__ == "__main__":
    unittest.main(verbosity=2)
