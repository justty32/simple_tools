"""JSON extension contract checks."""

import json
import os
from pathlib import Path
import tempfile
import unittest

from .. import jsonx


class JsonxTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="agent-machine-", dir="/tmp")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name: str, value: object) -> Path:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def test_env_and_default(self):
        os.environ["AGENT_MACHINE_TEST_VALUE"] = "from-env"
        self.addCleanup(os.environ.pop, "AGENT_MACHINE_TEST_VALUE", None)
        path = self.write("env.json", {
            "set": {"$env": "AGENT_MACHINE_TEST_VALUE"},
            "missing": {"$env": "AGENT_MACHINE_TEST_MISSING"},
            "default": {"$env": "AGENT_MACHINE_TEST_MISSING", "default": 7},
        })

        self.assertEqual(
            jsonx.read(path),
            {"set": "from-env", "missing": None, "default": 7},
        )

    def test_file_fragment_and_array_item_refs(self):
        self.write("parts/base.json", {"shared": {"n": 7}, "items": ["a", "b"]})
        path = self.write("main.json", {
            "part": {"$ref": "parts/base.json#shared"},
            "item": {"$ref": "parts/base.json#items/1"},
        })

        self.assertEqual(jsonx.read(path), {"part": {"n": 7}, "item": "b"})

    def test_ref_array_deep_merges_objects(self):
        self.write("base.json", {"endpoint": "a", "parameters": {"a": 1, "b": 1}})
        self.write("local.json", {"endpoint": "b", "parameters": {"b": 2}})
        path = self.write("llm.json", {"$ref": ["base.json", "local.json"]})

        self.assertEqual(jsonx.read(path), {
            "endpoint": "b",
            "parameters": {"a": 1, "b": 2},
        })

    def test_ref_can_replace_an_array_element(self):
        self.write("message.json", {"role": "system", "content": "be concise"})
        path = self.write("messages.json", [{"$ref": "message.json"}])

        self.assertEqual(
            jsonx.read(path),
            [{"role": "system", "content": "be concise"}],
        )

    def test_cycle_is_rejected(self):
        path = self.write("a.json", {"next": {"$ref": "b.json#next"}})
        self.write("b.json", {"next": {"$ref": "a.json#next"}})

        with self.assertRaisesRegex(ValueError, "迴圈"):
            jsonx.read(path)


if __name__ == "__main__":
    unittest.main()
