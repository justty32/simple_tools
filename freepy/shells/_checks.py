"""Offline checks for the human-facing shell helpers."""

import base_tools
import os
from pathlib import Path
import subprocess
import shells as shell_api
import sys
import tempfile

from agentloop import Handle
from agentloop._testing import FakeBot, response
from llms import Engine, to_tools

from . import Assistant, assistant, toolbox
from . import __main__ as launcher
from . import common
from ._checks_api import direct_api


def read_file(path: str) -> str:
    """Read one file."""
    return path


def write_file(path: str, content: str) -> str:
    """Write one file."""
    return content


def check(condition, label):
    if not condition:
        raise AssertionError(label)
    print(f"ok  {label}")


def rejects(call, text):
    try:
        call()
    except (TypeError, ValueError, RuntimeError) as exc:
        return text in str(exc)
    return False


def main():
    bundle = to_tools(write_file)
    schemas, dispatch = toolbox(read_file, bundle)
    check([x["function"]["name"] for x in schemas]
          == ["read_file", "write_file"], "toolbox preserves explicit order")
    check(dispatch == {"read_file": read_file, "write_file": write_file},
          "toolbox combines callable and bundle dispatch")
    check(rejects(lambda: toolbox(read_file, to_tools(read_file)), "duplicate"),
          "toolbox rejects names that would shadow an effect")
    check(rejects(lambda: toolbox(([schemas[0]], {})), "same names"),
          "toolbox rejects schema and dispatch mismatch")

    engine = Engine(model="offline", caps={"tools": True})
    bot, dispatch = assistant(
        engine, read_file, bundle, system="Be exact.")
    check(bot.engine is engine and bot.system == "Be exact.",
          "assistant preserves explicit engine and system")
    check(bot.tools == schemas and set(dispatch) == {"read_file", "write_file"},
          "assistant returns a matched bot and dispatch")
    setup = assistant(engine, read_file)
    check(isinstance(setup, Assistant) and tuple(setup) == (setup.bot,
                                                            setup.dispatch),
          "assistant remains unpackable while exposing a session helper")
    original_session = shell_api.session
    forwarded = []
    try:
        shell_api.session = lambda *args, **kwargs: forwarded.append(
            (args, kwargs)) or "controller"
        controller = setup.session(
            "inspect", daemon=True, name="repl-check")
    finally:
        shell_api.session = original_session
    check(controller == "controller"
          and forwarded == [((setup.bot, setup.dispatch, "inspect"), {
              "handle": None, "images": None, "daemon": True,
              "name": "repl-check"})],
          "Assistant.session forwards the matched pair and runner options")
    interactive = Assistant(
        FakeBot(response("first"), response("final")), {})
    controller = interactive.session(
        "inspect", handle=Handle(auto_finish=False))
    controller.handle.wait_for_state("waiting", timeout=1)
    controller.send("summarize", finish=True)
    result = controller.join(1)
    check(result.stop == "done" and result.text == "final"
          and interactive.bot.asked[1][0] == "summarize",
          "Assistant.session supports a waiting and send REPL flow")

    preset_bot, base_dispatch = assistant(
        "lm-gemma-4-12b", base_tools.tools())
    check(preset_bot.engine.model == "lm-gemma-4-12b"
          and set(base_dispatch) == {fn.__name__ for fn in base_tools.ALL},
          "documented preset and base-tools setup stays offline")
    check(rejects(lambda: assistant(object()), "Engine"),
          "assistant rejects ambiguous engine values")

    direct_api(check, rejects, read_file, bundle, schemas)

    chdirs = []
    executions = []
    old_chdir, old_replace = common.os.chdir, common.replace
    try:
        common.os.chdir = chdirs.append
        common.replace = lambda program, argv, env: executions.append(
            (program, argv, env))
        common.enter("python", "-i", cwd=None)
        check(not chdirs and executions[-1][1] == ["python", "-i"],
              "REPL entry can preserve the operator working directory")
        common.enter("pi")
        check(chdirs == [common.FREEPY],
              "coding-agent entries retain the FreePy working directory")
    finally:
        common.os.chdir, common.replace = old_chdir, old_replace

    routed = []
    old_launcher_replace = launcher.replace
    try:
        launcher.replace = lambda program, argv: routed.append((program, argv))
        launcher.main(["repl", "-c", "argument with spaces"])
    finally:
        launcher.replace = old_launcher_replace
    check(routed == [(sys.executable, [
              sys.executable, str(launcher.HERE / "repl.py"),
              "-c", "argument with spaces",
          ])], "module launcher preserves each pass-through argument")

    if sys.platform == "win32":
        with tempfile.TemporaryDirectory() as tmp:
            probe = Path(tmp) / "launcher probe.py"
            probe.write_text(
                "import sys\n"
                "print(repr(sys.argv[1:]))\n"
                "raise SystemExit(23)\n",
                encoding="utf-8")
            command = (
                "import sys; from shells.common import replace; "
                "replace(sys.executable, [sys.executable, "
                f"{str(probe)!r}, 'argument with spaces'])"
            )
            env = dict(os.environ, PYTHONPATH=os.pathsep.join(filter(None, [
                str(common.FREEPY), str(common.LLMKIT),
                os.environ.get("PYTHONPATH"),
            ])))
            result = subprocess.run(
                [sys.executable, "-c", command], env=env,
                capture_output=True, text=True)
        check(result.returncode == 23,
              "Windows launcher preserves the child exit status")
        check(result.stdout.strip() == "['argument with spaces']",
              "Windows launcher preserves arguments containing spaces")

        result = subprocess.run([
            sys.executable, "-m", "shells", "repl", "-c",
            "import sys; print(repr(sys.argv[1:]))",
            "argument with spaces",
        ], env=env, capture_output=True, text=True)
        check("['argument with spaces']" in result.stdout.splitlines(),
              "both Windows launcher layers preserve spaced arguments")

        with tempfile.TemporaryDirectory() as tmp:
            fake_pi = Path(tmp) / "pi.cmd"
            fake_pi.write_text("@exit /b 23\n", encoding="utf-8")
            env["PATH"] = os.pathsep.join([tmp, env.get("PATH", "")])
            env.pop("AGENTLOOP_PI_FACTORY", None)
            result = subprocess.run([
                sys.executable, "-m", "shells", "pi",
                "/d", "/c", "exit 23",
            ], env=env, capture_output=True, text=True)
        check(result.returncode == 23,
              "both Windows launcher layers preserve the child exit status")


if __name__ == "__main__":
    main()
