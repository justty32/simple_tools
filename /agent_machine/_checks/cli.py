"""Commands work across separate Linux processes."""

import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest


PROJECT = Path(__file__).parents[2]


class CommandTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="agent-machine-", dir="/tmp")
        self.root = Path(self.temp.name)
        self.bot = self.root / "bot-a"

    def tearDown(self):
        self.temp.cleanup()

    def command(self, *arguments: object) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, "-m", "agent_machine", *(str(item) for item in arguments)],
            cwd=PROJECT,
            check=True,
            capture_output=True,
            text=True,
        )

    def test_start_next_show_pause_resume_and_stop(self):
        self.command("new", self.bot)

        handle = Path(self.command("start", self.bot, "hello").stdout.strip())
        self.assertEqual(json.loads(self.command("show", handle).stdout)["status"], "ready")
        self.assertEqual(json.loads(self.command("pause", handle).stdout)["status"], "paused")
        self.assertEqual(json.loads(self.command("resume", handle).stdout)["status"], "ready")
        self.assertEqual(json.loads(self.command("next", handle).stdout)["status"], "done")
        self.assertEqual(json.loads(self.command("show", handle).stdout)["step"], 1)

        stopped = Path(self.command("start", self.bot, "later").stdout.strip())
        self.assertEqual(json.loads(self.command("stop", stopped).stdout)["status"], "stopped")

    def test_run_finishes_in_foreground(self):
        self.command("new", self.bot)

        result = self.command("run", self.bot, "do something")

        self.assertEqual(result.stdout, "do something\n")

    def test_generated_run_uses_installed_tool(self):
        self.command("new", self.bot)
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        tool = bin_dir / "agent-machine"
        shutil.copyfile(PROJECT / "agent-machine", tool)
        os.chmod(tool, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
        environment = dict(os.environ)
        environment["PATH"] = f"{bin_dir}:{environment['PATH']}"
        environment["PYTHONPATH"] = str(PROJECT)

        result = subprocess.run(
            [self.bot / "run", "from bot"],
            cwd=self.root,
            env=environment,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.stdout, "from bot\n")


if __name__ == "__main__":
    unittest.main()
