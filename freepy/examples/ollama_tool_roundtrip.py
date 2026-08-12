"""Manual Ollama tool-call probe with assertions and model cleanup.

This is deliberately not part of the offline test suite. It only runs against
an explicitly installed model and never asks Ollama to pull one.
"""

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "llmkit")]

from _ollama_probe import manual_probe, progress  # noqa: E402
from agentloop import Controller, Handle  # noqa: E402
from agentloop.limits import Limits  # noqa: E402
from llms import Engine, LLM, Params, to_tools  # noqa: E402

LIMITS = {
    "max_tokens_per_step": 4096,
    "steps": 16,
    "calls": 16,
    "seconds": 600,
}


def multiply(a: int, b: int) -> str:
    """Multiply two integers using this tool rather than mental arithmetic."""
    return str(a * b)


def run_round(host, model):
    schemas, dispatch = to_tools(multiply)
    engine = Engine(
        model, url=host + "/v1", key="ollama", timeout=300,
        params=Params(
            temperature=0, max_tokens=LIMITS["max_tokens_per_step"]),
        caps={"tools": True})
    bot = LLM(
        engine, tools=schemas,
        system=("You are a strict integration-test agent. You must use the "
                "provided tool; never calculate the answer yourself."))
    handle = Handle()
    Limits(
        steps=LIMITS["steps"], calls=LIMITS["calls"],
        seconds=LIMITS["seconds"], tools=dispatch, engines=[model],
    ).attach(handle)
    handle.after_step.append(lambda h: progress(
        f"step={h.step} requested={len(h.tool_calls)} "
        f"output_tokens={h.output_tokens}"))
    handle.after_tools.append(lambda h: progress(
        f"calls={h.calls} used={json.dumps(h.used, sort_keys=True)}"))

    result = Controller(
        bot, dispatch,
        "Call multiply with a=6 and b=7. After receiving the tool result, "
        "reply exactly TOOL_OK=42.",
        handle=handle,
    ).run()
    passed = (
        result.state == "completed"
        and result.err is None
        and result.used == {"multiply": 1}
        and result.tool_log == [(1, "multiply", {"a": 6, "b": 7}, "42")]
        and result.text.strip() == "TOOL_OK=42"
    )
    return {
        "assertions_passed": passed,
        "state": result.state,
        "stop": result.stop,
        "steps": result.step,
        "calls": result.calls,
        "used": result.used,
        "input_tokens": result.input_tokens,
        "output_tokens": result.output_tokens,
        "elapsed_seconds": round(result.elapsed(), 2),
        "answer": result.text,
        "tool_log": result.tool_log,
        "round_error": None if result.err is None else repr(result.err),
    }


def main(argv=None):
    return manual_probe(
        "Run a manual FreePy tool-call roundtrip against Ollama.",
        LIMITS, run_round, argv)


if __name__ == "__main__":
    raise SystemExit(main())
