from __future__ import annotations

import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from p1a2_process_runtime import ProcessRuntime
from test_support import exited, root_call

HERE = Path(__file__).resolve().parent
DRIVER = """import json,sys
from pathlib import Path
from p1a2_process_runtime import ProcessRuntime
base=Path(sys.argv[1])
call=json.loads((base/'call.json').read_text(encoding='utf-8'))
ProcessRuntime(base/'store').accept_fixture_root('root',lambda:call)
"""


class AfterSpawnKill(unittest.TestCase):
    def test_side_effect_survives_killed_writer_without_respawn(self) -> None:
        with tempfile.TemporaryDirectory(dir="/tmp") as temporary:
            base = Path(temporary); counter = base / "counter"; fixture = base / "kill-parent"
            fixture.write_text("#!/usr/bin/python3\nimport os,signal,sys\np=sys.argv[1];fd=os.open(p,os.O_WRONLY|os.O_CREAT|os.O_APPEND,0o600);os.write(fd,b'x');os.fsync(fd);os.close(fd)\nfd=os.open(os.path.dirname(p),os.O_RDONLY|os.O_DIRECTORY);os.fsync(fd);os.close(fd)\nos.kill(os.getppid(),signal.SIGKILL);os._exit(0)\n", encoding="utf-8")
            fixture.chmod(0o755)
            call = root_call(str(fixture), str(base), b"")
            call["children"][0]["recipe"]["argv"] = [str(fixture), str(counter)]
            call["first_success_oracle"] = exited(0, b"never")
            (base / "call.json").write_text(json.dumps(call), encoding="utf-8")
            env = dict(os.environ); env["PYTHONPATH"] = str(HERE); env["PYTHONDONTWRITEBYTECODE"] = "1"
            crashed = subprocess.Popen([sys.executable, "-c", DRIVER, str(base)], env=env, start_new_session=True)
            self.assertEqual(crashed.wait(timeout=10), -signal.SIGKILL)
            time.sleep(0.05)
            with self.assertRaises(ProcessLookupError): os.killpg(crashed.pid, 0)
            report = ProcessRuntime(base / "store").recover_store()
            self.assertEqual(report["roots"][0]["state"], "waiting_for_child_repair_incomplete_evidence")
            self.assertEqual(counter.read_bytes(), b"x")
            p0 = base / "store" / "tasks" / "root--first" / "attempts" / "attempt-1" / "p0"
            self.assertEqual(len(list(p0.iterdir())), 1)
            first = (base / "store" / "tasks" / "root--first" / "events.jsonl").read_text()
            root = (base / "store" / "tasks" / "root" / "events.jsonl").read_text()
            self.assertIn("incomplete_evidence", first); self.assertNotIn("receipt_committed", first)
            self.assertNotIn("child_observed", root); self.assertFalse((base / "store" / "tasks" / "root--second").exists())
            self.assertEqual(ProcessRuntime(base / "store").recover_store(), report)
            self.assertEqual(counter.read_bytes(), b"x")


if __name__ == "__main__": unittest.main(verbosity=2)
