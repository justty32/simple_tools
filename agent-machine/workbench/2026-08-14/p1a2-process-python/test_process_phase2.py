from __future__ import annotations

import json
import shutil
import tempfile
import unittest
from pathlib import Path

from p1a2_process_binder import IncompleteUnknown, bind
from p1a2_process_runtime import ProcessRuntime
from p1a2_store import Corruption, strict_json
from test_support import exited, file_tree, root_call


class ProcessPhase2(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(dir="/tmp")
        self.base = Path(self.temp.name)
        self.counter = self.base / "counter"
        self.fixture = self.base / "fixture"
        self.fixture.write_text("#!/usr/bin/python3\nimport pathlib,sys\np=pathlib.Path(sys.argv[1]);p.write_text(p.read_text()+'x' if p.exists() else 'x')\nsys.stdin.buffer.read();sys.stdout.buffer.write(b'OK\\n')\n", encoding="utf-8")
        self.fixture.chmod(0o755)
        self.call = root_call(str(self.fixture), str(self.base), b"in")
        self.call["children"][0]["recipe"]["argv"] = [str(self.fixture), str(self.counter)]
        self.call["first_success_oracle"] = exited(0, b"OK\n")
        self.store = self.base / "store"
        self.runtime = ProcessRuntime(self.store)
        self.runtime.accept_fixture_root("root", lambda: self.call)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def raw(self) -> tuple[dict, Path]:
        task = self.store / "tasks" / "root--first"
        call = strict_json((task / "call.json").read_bytes(), "Call")
        uuid_dir = next((task / "attempts" / "attempt-1" / "p0").iterdir())
        return call, uuid_dir

    def rewind_to_raw(self) -> None:
        root = self.store / "tasks" / "root"; first = self.store / "tasks" / "root--first"
        (root / "events.jsonl").write_bytes(b"\n".join((root / "events.jsonl").read_bytes().splitlines()[:3]) + b"\n")
        (first / "events.jsonl").write_bytes(b"\n".join((first / "events.jsonl").read_bytes().splitlines()[:2]) + b"\n")
        shutil.rmtree(self.store / "tasks" / "root--second"); shutil.rmtree(root / "payload")

    def test_happy_path_and_recovery_are_stable(self) -> None:
        self.assertEqual(self.counter.read_text(), "x")
        self.assertEqual(self.runtime.recover_store()["roots"][0]["state"], "receipt_committed")
        self.assertEqual(self.counter.read_text(), "x")
        self.assertEqual(len(list((self.store / "tasks" / "root--first" / "attempts" / "attempt-1" / "p0").iterdir())), 1)

    def test_known_raw_recommits_without_second_p0_invocation(self) -> None:
        root_events = self.store / "tasks" / "root" / "events.jsonl"
        first_events = self.store / "tasks" / "root--first" / "events.jsonl"
        root_events.write_bytes(b"\n".join(root_events.read_bytes().splitlines()[:3]) + b"\n")
        first_events.write_bytes(b"\n".join(first_events.read_bytes().splitlines()[:2]) + b"\n")
        shutil.rmtree(self.store / "tasks" / "root--second")
        shutil.rmtree(self.store / "tasks" / "root" / "payload")
        self.assertEqual(self.runtime.recover_store()["roots"][0]["state"], "receipt_committed")
        self.assertEqual(self.counter.read_text(), "x")

    def test_incomplete_prefix_is_unknown(self) -> None:
        call, raw = self.raw()
        for name in ("stdout.bin", "stderr.bin", "receipt.json", "receipt-ready.json", "terminal.json"):
            (raw / name).unlink()
        result = bind(call, raw.parents[2])
        self.assertIsInstance(result, IncompleteUnknown)

    def test_tampered_stream_and_prefix_binding_are_contradictions(self) -> None:
        call, raw = self.raw(); before = (raw / "stdout.bin").read_bytes()
        (raw / "stdout.bin").write_bytes(b"tampered")
        with self.assertRaises(Corruption): bind(call, raw.parents[2])
        self.assertEqual((raw / "stdout.bin").read_bytes(), b"tampered")
        (raw / "stdout.bin").write_bytes(before)
        request = json.loads((raw / "request.json").read_text())
        for key, value in (("argv", ["wrong"]), ("cwd", "/wrong"), ("executable", "/wrong")):
            changed = dict(request); changed[key] = value
            (raw / "request.json").write_text(json.dumps(changed), encoding="utf-8")
            with self.subTest(key=key), self.assertRaises(Corruption): bind(call, raw.parents[2])
        (raw / "request.json").write_text(json.dumps(request), encoding="utf-8")
        (raw / "stdin.bin").write_bytes(b"wrong")
        with self.assertRaises(Corruption): bind(call, raw.parents[2])

    def test_outcome_unknown_is_a_contradiction(self) -> None:
        call, raw = self.raw(); receipt = json.loads((raw / "receipt.json").read_text())
        receipt["outcome"] = {"kind": "outcome_unknown", "reason": "forbidden"}
        (raw / "receipt.json").write_text(json.dumps(receipt), encoding="utf-8")
        with self.assertRaises(Corruption): bind(call, raw.parents[2])

    def test_receipt_version_bool_and_nonobject_outcome_are_contradictions(self) -> None:
        call, raw = self.raw(); receipt = json.loads((raw / "receipt.json").read_text())
        for key, value in (("version", True), ("outcome", [])):
            changed = dict(receipt); changed[key] = value
            (raw / "receipt.json").write_text(json.dumps(changed), encoding="utf-8")
            with self.subTest(key=key), self.assertRaises(Corruption): bind(call, raw.parents[2])

    def test_runtime_tamper_before_receipt_writes_nothing(self) -> None:
        call, raw = self.raw(); self.rewind_to_raw(); (raw / "stdout.bin").write_bytes(b"tamper")
        before = file_tree(self.store)
        with self.assertRaises(Corruption): ProcessRuntime(self.store).recover_store()
        self.assertEqual(file_tree(self.store), before)
        self.assertFalse((self.store / "tasks" / "root--second").exists())

    def test_runtime_request_mismatch_before_receipt_writes_nothing(self) -> None:
        _, raw = self.raw(); self.rewind_to_raw(); request = json.loads((raw / "request.json").read_text()); request["argv"] = ["evil"]
        (raw / "request.json").write_text(json.dumps(request), encoding="utf-8"); before = file_tree(self.store)
        with self.assertRaises(Corruption): ProcessRuntime(self.store).recover_store()
        self.assertEqual(file_tree(self.store), before)

    def test_runtime_outcome_unknown_before_receipt_writes_nothing(self) -> None:
        _, raw = self.raw(); self.rewind_to_raw(); receipt = json.loads((raw / "receipt.json").read_text()); receipt["outcome"] = {"kind": "outcome_unknown", "reason": "x"}
        (raw / "receipt.json").write_text(json.dumps(receipt), encoding="utf-8"); before = file_tree(self.store)
        with self.assertRaises(Corruption): ProcessRuntime(self.store).recover_store()
        self.assertEqual(file_tree(self.store), before)


if __name__ == "__main__": unittest.main(verbosity=2)
