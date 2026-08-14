"""File-backed run state and its small state transitions."""

import json
import os
from pathlib import Path
import tempfile

from .bot import load_bot
from .llm import ask


STATE = "state.json"
FINISHED = {"done", "stopped"}


def start(bot_path: str | Path, instruction: str) -> Path:
    if not isinstance(instruction, str):
        raise TypeError("instruction 必須是字串")
    if not instruction:
        raise ValueError("instruction 不可為空")
    bot = load_bot(bot_path)
    root = bot.path / ".agent-machine" / "runs"
    root.mkdir(parents=True, exist_ok=True)
    handle = Path(tempfile.mkdtemp(prefix="run-", dir=root))
    state = {
        "version": 1,
        "handle": str(handle),
        "bot": str(bot.path),
        "status": "ready",
        "step": 0,
        "messages": [*bot.messages, {"role": "user", "content": instruction}],
    }
    _write(handle, state)
    return handle


def show(handle: str | Path) -> dict:
    path = Path(handle).resolve() / STATE
    with path.open(encoding="utf-8") as source:
        state = json.load(source)
    if not isinstance(state, dict) or state.get("version") != 1:
        raise ValueError(f"無法讀取 run state: {path}")
    return state


def next_step(handle: str | Path) -> dict:
    path = Path(handle).resolve()
    state = show(path)
    if state["status"] != "ready":
        return state
    bot = load_bot(state["bot"])
    reply = ask(bot.llm, state["messages"], bot.tools)
    state["messages"].append(reply)
    state["step"] += 1
    state["status"] = "done"
    _write(path, state)
    return state


def pause(handle: str | Path) -> dict:
    return _set_status(handle, "ready", "paused")


def resume(handle: str | Path) -> dict:
    return _set_status(handle, "paused", "ready")


def stop(handle: str | Path) -> dict:
    path = Path(handle).resolve()
    state = show(path)
    if state["status"] not in FINISHED:
        state["status"] = "stopped"
        _write(path, state)
    return state


def _set_status(handle: str | Path, old: str, new: str) -> dict:
    path = Path(handle).resolve()
    state = show(path)
    if state["status"] == old:
        state["status"] = new
        _write(path, state)
    return state


def _write(handle: Path, state: dict) -> None:
    target = handle / STATE
    descriptor, temporary = tempfile.mkstemp(prefix=".state-", dir=handle)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
            json.dump(state, output, ensure_ascii=False, indent=2)
            output.write("\n")
        os.replace(temporary, target)
    except BaseException:
        if os.path.exists(temporary):
            os.unlink(temporary)
        raise
