"""Manual Ollama tool-call probe with assertions and model cleanup.

This is deliberately not part of the offline test suite.  It only runs against
an explicitly installed model and never asks Ollama to pull one.
"""

import argparse
import json
from pathlib import Path
import sys
import time
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "llmkit")]

from agentloop import Controller, Handle  # noqa: E402
from agentloop.limits import Limits  # noqa: E402
from llms import Engine, LLM, Params, to_tools  # noqa: E402

MAX_TOKENS = 4096
ROUND_STEPS = 16
ROUND_CALLS = 16
ROUND_SECONDS = 600


def multiply(a: int, b: int) -> str:
    """Multiply two integers using this tool rather than mental arithmetic."""
    return str(a * b)


def request_json(host, path, body=None, *, timeout=30):
    data = None if body is None else json.dumps(body).encode()
    request = Request(
        host + path, data=data, headers={"Content-Type": "application/json"})
    with urlopen(request, timeout=timeout) as response:
        return json.loads(response.read())


def model_names(response):
    return [item.get("name") or item.get("model")
            for item in response.get("models", [])]


def unload_and_wait(host, model, *, attempts=30):
    request_json(host, "/api/generate", {"model": model, "keep_alive": 0})
    for _ in range(attempts):
        remaining = model_names(request_json(host, "/api/ps"))
        if model not in remaining:
            return remaining
        time.sleep(0.5)
    return remaining


def progress(message):
    print(message, file=sys.stderr, flush=True)


def run_probe(host, model):
    schemas, dispatch = to_tools(multiply)
    engine = Engine(
        model, url=host + "/v1", key="ollama", timeout=300,
        params=Params(temperature=0, max_tokens=MAX_TOKENS),
        caps={"tools": True})
    bot = LLM(
        engine, tools=schemas,
        system=("You are a strict integration-test agent. You must use the "
                "provided tool; never calculate the answer yourself."))
    handle = Handle()
    Limits(
        steps=ROUND_STEPS, calls=ROUND_CALLS, seconds=ROUND_SECONDS,
        tools=dispatch, engines=[model]).attach(handle)
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


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Run a manual FreePy tool-call roundtrip against Ollama.")
    parser.add_argument(
        "--model", required=True,
        help="Exact name of a model already present in Ollama; never pulled.")
    parser.add_argument(
        "--host", default="http://127.0.0.1:11434",
        help="Ollama base URL (default: http://127.0.0.1:11434).")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    host = args.host.rstrip("/")
    report = {
        "ok": False,
        "host": host,
        "model": args.model,
        "limits": {
            "max_tokens_per_step": MAX_TOKENS,
            "steps": ROUND_STEPS,
            "calls": ROUND_CALLS,
            "seconds": ROUND_SECONDS,
        },
        "preflight_loaded": None,
        "round": None,
        "remaining_models": None,
        "unloaded": None,
        "error": None,
    }
    started = False
    try:
        installed = model_names(request_json(host, "/api/tags"))
        if args.model not in installed:
            raise RuntimeError(
                f"model is not installed; refusing to pull it: {args.model}")
        loaded = model_names(request_json(host, "/api/ps"))
        report["preflight_loaded"] = loaded
        if loaded:
            raise RuntimeError(
                f"refusing to start while Ollama has loaded models: {loaded}")

        progress(f"running model={args.model} host={host}")
        started = True
        report["round"] = run_probe(host, args.model)
    except (Exception, KeyboardInterrupt) as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        if started:
            try:
                remaining = unload_and_wait(host, args.model)
                report["remaining_models"] = remaining
                report["unloaded"] = args.model not in remaining
            except Exception as exc:
                report["unloaded"] = False
                cleanup_error = f"{type(exc).__name__}: {exc}"
                report["error"] = (
                    cleanup_error if report["error"] is None
                    else report["error"] + f"; cleanup failed: {cleanup_error}")

    round_ok = report["round"] is not None and report["round"]["assertions_passed"]
    report["ok"] = bool(round_ok and report["unloaded"] and report["error"] is None)
    print(json.dumps(report, ensure_ascii=False, sort_keys=True))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
