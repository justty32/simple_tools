"""Shared lifecycle for manual Ollama examples; not a public FreePy API."""

import argparse
import json
import sys
import time
from urllib.request import Request, urlopen


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


def _parse_args(description, argv):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument(
        "--model", required=True,
        help="Exact name of a model already present in Ollama; never pulled.")
    parser.add_argument(
        "--host", default="http://127.0.0.1:11434",
        help="Ollama base URL (default: http://127.0.0.1:11434).")
    return parser.parse_args(argv)


def manual_probe(description, limits, run_round, argv=None):
    """Run one asserted round under the common preflight/cleanup contract."""
    args = _parse_args(description, argv)
    host = args.host.rstrip("/")
    report = {
        "ok": False,
        "host": host,
        "model": args.model,
        "limits": dict(limits),
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
        report["round"] = run_round(host, args.model)
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

    round_ok = (
        isinstance(report["round"], dict)
        and report["round"].get("assertions_passed") is True)
    report["ok"] = bool(
        round_ok and report["unloaded"] and report["error"] is None)
    print(json.dumps(report, ensure_ascii=False, sort_keys=True))
    return 0 if report["ok"] else 1
