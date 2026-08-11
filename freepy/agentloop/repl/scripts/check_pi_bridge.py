#!/usr/bin/env python3
"""Offline end-to-end check for the Pi extension's Python bridge protocol."""

import json
import os
from pathlib import Path
import subprocess
import sys


HERE = Path(__file__).resolve().parent
FREEPY = HERE.parents[2]
BRIDGE = HERE / "pi_bridge.py"
FACTORY = "agentloop.repl.examples.pi_minimal_factory:create"


def main():
    env = dict(os.environ)
    paths = [str(FREEPY), str(FREEPY / "llmkit")]
    if env.get("PYTHONPATH"):
        paths.append(env["PYTHONPATH"])
    env["PYTHONPATH"] = os.pathsep.join(paths)
    process = subprocess.Popen(
        [sys.executable, str(BRIDGE), "--factory", FACTORY],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    next_id = 0

    def call(action, **fields):
        nonlocal next_id
        next_id += 1
        request = {"id": next_id, "action": action, **fields}
        process.stdin.write(json.dumps(request) + "\n")
        process.stdin.flush()
        line = process.stdout.readline()
        if not line:
            raise AssertionError(
                f"bridge exited without response: {process.stderr.read()}")
        response = json.loads(line)
        assert response["id"] == next_id, response
        return response

    try:
        absent = call("status")
        assert absent["ok"] and absent["result"]["state"] == "absent", absent

        started = call("start", prompt="demo")
        assert started["ok"], started
        first = call("wait", after=0, timeout=2)["result"]["event"]
        assert first["boundary"] == "after_step", first
        assert first["snapshot"]["active"], first
        status = call("status")["result"]
        assert status["state"] == "paused" and status["step"] == 1, status

        changed_call = {
            "id": "demo_call",
            "name": "echo",
            "args": {"text": "changed by protocol"},
        }
        edited = call(
            "edit",
            expected_step=1,
            expected_state="paused",
            set={"tool_calls": [changed_call]},
        )
        assert edited["ok"], edited
        stale = call("edit", expected_step=99, set={"prompt": "too late"})
        assert not stale["ok"] and "stale edit" in stale["error"]["message"], stale
        invalid = call("edit", set={"prompt": 42})
        assert not invalid["ok"] and "prompt must be" in invalid["error"]["message"], invalid

        resumed = call("resume")
        assert resumed["result"]["accepted"], resumed
        tools = call("wait", after=first["sequence"], timeout=2)["result"]["event"]
        assert tools["boundary"] == "after_tools", tools
        assert tools["snapshot"]["tool_results"]["demo_call"] == (
            "echo: changed by protocol"), tools
        final_step = call(
            "wait", after=tools["sequence"], timeout=2)["result"]["event"]
        assert final_step["boundary"] == "after_step", final_step
        assert "changed by protocol" in final_step["snapshot"]["message"], final_step
        timed_out = call(
            "wait", after=final_step["sequence"], timeout=0.01)["result"]
        assert timed_out["event"] is None, timed_out
        assert timed_out["snapshot"]["state"] == "paused", timed_out

        ended = call("end", reason="smoke")
        assert ended["result"]["accepted"], ended
        joined = call("join", timeout=2)
        assert joined["result"]["joined"], joined
        assert joined["result"]["snapshot"]["stop"] == "smoke", joined
        print("Pi bridge smoke test passed")
    finally:
        if process.poll() is None:
            try:
                call("shutdown", timeout=1)
            except (AssertionError, BrokenPipeError):
                pass
            process.stdin.close()
            try:
                process.wait(2)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(2)
        stderr = process.stderr.read()
        if process.returncode:
            raise AssertionError(
                f"bridge exited with {process.returncode}: {stderr}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
