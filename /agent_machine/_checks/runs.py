"""Bot loading and file-backed run checks."""

import json
import os
from pathlib import Path
import tempfile
import unittest

from ..bot import load_bot
from ..new import create_function
from ..runs import next_step, pause, resume, show, start, stop


class RunTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="agent-machine-", dir="/tmp")
        self.bot = Path(self.temp.name) / "bot-a"
        create_function(self.bot)

    def tearDown(self):
        self.temp.cleanup()

    def write_json(self, relative: str, value: object) -> None:
        path = self.bot / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding="utf-8")

    def test_bot_loads_three_jsonx_files(self):
        os.environ["AGENT_MACHINE_TEST_ENGINE"] = "echo"
        self.addCleanup(os.environ.pop, "AGENT_MACHINE_TEST_ENGINE", None)
        self.write_json("llm.json", {"engine": {"$env": "AGENT_MACHINE_TEST_ENGINE"}})
        self.write_json("parts/system.json", {"role": "system", "content": "short"})
        self.write_json("messages.json", [{"$ref": "parts/system.json"}])

        bot = load_bot(self.bot)

        self.assertEqual(bot.llm, {"engine": "echo"})
        self.assertEqual(bot.messages, [{"role": "system", "content": "short"}])
        self.assertEqual(bot.tools, [])

    def test_start_returns_path_to_persistent_ready_run(self):
        handle = start(self.bot, "hello")

        self.assertTrue(handle.is_dir())
        self.assertEqual(handle.parent, self.bot / ".agent-machine" / "runs")
        self.assertEqual(show(handle), {
            "version": 1,
            "handle": str(handle),
            "bot": str(self.bot),
            "status": "ready",
            "step": 0,
            "messages": [{"role": "user", "content": "hello"}],
        })

    def test_next_runs_one_echo_step_and_does_not_repeat(self):
        handle = start(self.bot, "hello")

        done = next_step(handle)
        repeated = next_step(handle)

        self.assertEqual(done["status"], "done")
        self.assertEqual(done["step"], 1)
        self.assertEqual(done["messages"][-1], {
            "role": "assistant",
            "content": "hello",
        })
        self.assertEqual(repeated, done)

    def test_pause_resume_and_stop_are_safe_to_repeat(self):
        handle = start(self.bot, "hello")

        self.assertEqual(pause(handle)["status"], "paused")
        self.assertEqual(pause(handle)["status"], "paused")
        self.assertEqual(next_step(handle)["status"], "paused")
        self.assertEqual(resume(handle)["status"], "ready")
        self.assertEqual(resume(handle)["status"], "ready")
        self.assertEqual(stop(handle)["status"], "stopped")
        self.assertEqual(stop(handle)["status"], "stopped")
        self.assertEqual(next_step(handle)["status"], "stopped")


if __name__ == "__main__":
    unittest.main()
