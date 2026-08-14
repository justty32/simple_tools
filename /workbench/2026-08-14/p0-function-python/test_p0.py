from __future__ import annotations

import json
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest

from aos_p0 import FunctionStore, Request, SimulatedCrash


HERE = Path(__file__).resolve().parent
CHILD = str(HERE / "fixture_child.py")


class FunctionPrototypeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(dir="/tmp")
        self.base = Path(self.temp.name)
        self.store = FunctionStore(self.base / "memory")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def request(self, *child_args: str, stdin: bytes = b"", cwd: str | None = None) -> Request:
        return Request(sys.executable, [sys.executable, CHILD, *child_args], stdin, cwd)

    def test_literal_argv_is_never_a_shell(self) -> None:
        values = ["", "two words", "*.py", "$(whoami)", "; echo nope", "$HOME"]
        _, receipt = self.store.run(self.request("argv", *values))
        self.assertEqual(receipt["outcome"], {"kind": "exited", "code": 0})
        directory = next(self.store.root.iterdir())
        self.assertEqual(json.loads((directory / "stdout.bin").read_text()), values)

    def test_raw_binary_stdin_including_nul_and_invalid_utf8(self) -> None:
        payload = b"prefix\x00\xff\xfe\x80suffix"
        invocation_id, receipt = self.store.run(self.request("echo", stdin=payload))
        self.assertEqual(receipt["stdout"]["size"], len(payload))
        self.assertEqual((self.store.root / invocation_id / "stdout.bin").read_bytes(), payload)

    def test_large_separate_streams_and_nonzero_exit_are_a_receipt(self) -> None:
        amount = 200_000
        invocation_id, receipt = self.store.run(self.request("split", str(amount)))
        self.assertEqual(receipt["outcome"], {"kind": "exited", "code": 7})
        directory = self.store.root / invocation_id
        self.assertEqual((directory / "stdout.bin").stat().st_size, amount)
        self.assertEqual((directory / "stderr.bin").stat().st_size, amount)
        self.assertTrue((directory / "terminal.json").exists())

    def test_signal_and_enoent_are_distinct(self) -> None:
        _, signaled = self.store.run(self.request("sigterm"))
        self.assertEqual(signaled["outcome"]["kind"], "signaled")
        self.assertEqual(signaled["outcome"]["signal"], 15)
        _, absent = self.store.run(Request("/definitely/not/a/program", ["missing"], b""))
        self.assertEqual(absent["outcome"]["kind"], "spawn_error")
        self.assertEqual(absent["outcome"]["errno"], 2)

    def test_explicit_cwd(self) -> None:
        cwd = self.base / "chosen-cwd"
        cwd.mkdir()
        _, receipt = self.store.run(self.request("cwd", cwd=str(cwd)))
        self.assertEqual(receipt["outcome"]["code"], 0)
        invocation = max(self.store.root.iterdir(), key=lambda path: path.stat().st_mtime_ns)
        self.assertEqual((invocation / "stdout.bin").read_text().strip(), str(cwd))

    def test_recover_intent_without_receipt_marks_unknown_without_rerun(self) -> None:
        token = self.base / "token"
        with self.assertRaises(SimulatedCrash) as caught:
            self.store.run(self.request("side-effect", str(token), "0"), crash_at="after_intent")
        invocation_id = str(caught.exception)
        self.assertFalse(token.exists())
        recovered = self.store.recover(invocation_id)
        self.assertEqual(recovered["receipt"]["outcome"]["kind"], "outcome_unknown")
        self.assertTrue(recovered["terminal"])
        self.assertFalse(token.exists())

    def test_recover_receipt_without_terminal_only_projects_terminal(self) -> None:
        token = self.base / "token"
        with self.assertRaises(SimulatedCrash) as caught:
            self.store.run(self.request("side-effect", str(token), "0"), crash_at="after_receipt")
        invocation_id = str(caught.exception)
        before = self.store.inspect(invocation_id)
        self.assertTrue(before["receipt_complete"])
        self.assertFalse(before["terminal"])
        recovered = self.store.recover(invocation_id)
        self.assertTrue(recovered["terminal"])
        self.assertEqual(token.read_text(encoding="utf-8"), "executed")

    def test_recover_receipt_json_before_ready_validates_and_preserves_output(self) -> None:
        with self.assertRaises(SimulatedCrash) as caught:
            self.store.run(self.request("split", "200"), crash_at="after_receipt_json")
        invocation_id = str(caught.exception)
        before = self.store.inspect(invocation_id)
        self.assertTrue(before["receipt_valid"])
        self.assertFalse(before["receipt_complete"])
        self.assertFalse(before["terminal"])
        recovered = self.store.recover(invocation_id)
        self.assertTrue(recovered["receipt_complete"])
        self.assertEqual(recovered["receipt"]["outcome"], {"kind": "exited", "code": 7})
        directory = self.store.root / invocation_id
        self.assertEqual((directory / "stdout.bin").read_bytes(), b"O" * 200)
        self.assertEqual((directory / "stderr.bin").read_bytes(), b"E" * 200)
        self.assertFalse((directory / "quarantine").exists())

    def test_recover_partial_stdout_and_invalid_receipt_quarantines_then_marks_unknown(self) -> None:
        with self.assertRaises(SimulatedCrash) as caught:
            self.store.run(self.request("echo", stdin=b"do-not-rerun"), crash_at="after_intent")
        invocation_id = str(caught.exception)
        directory = self.store.root / invocation_id
        self.store._publish_bytes(directory, "stdout.bin", b"partial")
        self.store._publish_json(directory, "receipt.json", {
            "version": 0, "outcome": {"kind": "exited", "code": 0},
            "stdout": {"sha256": "0" * 64, "size": 999}, "stderr": {"sha256": "0" * 64, "size": 0},
        })
        (directory / ".stderr.bin.interrupted").write_bytes(b"unpublished")
        self.assertFalse(self.store.inspect(invocation_id)["receipt_valid"])
        recovered = self.store.recover(invocation_id)
        self.assertEqual(recovered["receipt"]["outcome"]["kind"], "outcome_unknown")
        self.assertTrue(recovered["terminal"])
        self.assertFalse((directory / "stdout.bin").exists())
        self.assertIn("outcome_unknown", (directory / "receipt.json").read_text(encoding="utf-8"))
        quarantined = {path.name for path in (directory / "quarantine").iterdir()}
        self.assertIn("stdout.bin", quarantined)
        self.assertIn("receipt.json", quarantined)
        self.assertIn(".stderr.bin.interrupted", quarantined)

    def test_invocation_id_table_cannot_escape_root(self) -> None:
        outside = self.base / "outside"
        outside.mkdir()
        sentinel = outside / "sentinel.bin"
        sentinel.write_bytes(b"outside-must-not-change")
        before = [(path.relative_to(outside).as_posix(), path.read_bytes()) for path in outside.rglob("*") if path.is_file()]

        valid_id, _ = self.store.run(self.request("echo", stdin=b"valid"))
        self.assertIsNotNone(re.fullmatch(r"[0-9a-f]{32}", valid_id))
        invalid_ids = {
            "parent": "../outside",
            "absolute": str(outside),
            "nested": valid_id + "/nested",
            "wrong_character": "g" * 32,
            "wrong_case": "A" * 32,
            "too_short": "a" * 31,
            "too_long": "a" * 33,
        }
        for label, invocation_id in invalid_ids.items():
            with self.subTest(label=label):
                for action in (self.store.inspect, self.store.recover):
                    with self.assertRaises(ValueError):
                        action(invocation_id)
        self.assertTrue(self.store.inspect(valid_id)["terminal"])
        self.assertTrue(self.store.recover(valid_id)["terminal"])
        after = [(path.relative_to(outside).as_posix(), path.read_bytes()) for path in outside.rglob("*") if path.is_file()]
        self.assertEqual(after, before)

    def test_invocation_directory_symlink_is_rejected(self) -> None:
        outside = self.base / "outside"
        outside.mkdir()
        (outside / "sentinel").write_text("unchanged", encoding="utf-8")
        invocation_id = "c" * 32
        (self.store.root / invocation_id).symlink_to(outside, target_is_directory=True)
        for action in (self.store.inspect, self.store.recover):
            with self.subTest(action=action.__name__):
                with self.assertRaises(ValueError):
                    action(invocation_id)
        self.assertEqual((outside / "sentinel").read_text(encoding="utf-8"), "unchanged")


if __name__ == "__main__":
    unittest.main(verbosity=2)
