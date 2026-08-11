#!/usr/bin/env python3
"""JSONL bridge between a Pi extension and one live agentloop Handle."""

import argparse
from collections import deque
from copy import deepcopy
import importlib
import inspect
import json
import os
import sys
import threading
import time

import agentloop
from agentloop.threading import BackgroundRun


MAX_EVENTS = 100
MAX_STRING = 8_000
EDIT_FIELDS = {
    "prompt", "images", "ask_options", "tool_calls", "tool_results",
    "auto_finish",
}


class ProtocolError(ValueError):
    pass


class Bridge:
    def __init__(self, factory_spec=None):
        self.factory_spec = factory_spec
        self.handle = None
        self.runner = None
        self.round_id = 0
        self._events = deque(maxlen=MAX_EVENTS)
        self._events_condition = threading.Condition()
        self._sequence = 0
        self._dropped = 0

    def dispatch(self, request):
        if not isinstance(request, dict):
            raise ProtocolError("request must be a JSON object")
        action = request.get("action")
        if not isinstance(action, str):
            raise ProtocolError("action must be a string")
        handler = getattr(self, f"action_{action}", None)
        if handler is None:
            raise ProtocolError(f"unknown action: {action}")
        return handler(request)

    def action_start(self, request):
        if self.runner is not None and self.runner.is_alive():
            raise ProtocolError("a Round is already active")
        factory_spec = self.factory_spec
        if not factory_spec:
            raise ProtocolError(
                "no factory configured; set AGENTLOOP_PI_FACTORY or --factory")
        factory = _load_factory(factory_spec)
        config = request.get("config") or {}
        if not isinstance(config, dict):
            raise ProtocolError("config must be an object")
        made = factory(config) if _accepts_argument(factory) else factory()
        try:
            bot, dispatch = made
        except (TypeError, ValueError) as err:
            raise ProtocolError("factory must return (bot, dispatch)") from err
        if not isinstance(dispatch, dict):
            raise ProtocolError("factory dispatch must be a dict")

        gates = request.get("gates") or {}
        if not isinstance(gates, dict):
            raise ProtocolError("gates must be an object")
        after_step = _gate("after_step", gates.get("after_step", "pause"))
        after_tools = _gate("after_tools", gates.get("after_tools", "continue"))
        auto_finish = request.get("auto_finish", False)
        if not isinstance(auto_finish, bool):
            raise ProtocolError("auto_finish must be a boolean")
        _validate_changes({
            name: request[name] for name in ("prompt", "images")
            if name in request
        })

        self.round_id += 1
        with self._events_condition:
            self._events.clear()
            self._dropped = 0
        handle = agentloop.Handle(auto_finish=auto_finish)
        handle.after_step.append(
            lambda h: self._on_boundary("after_step", h, after_step))
        handle.after_tools.append(
            lambda h: self._on_boundary("after_tools", h, after_tools))
        self.handle = handle
        self.runner = BackgroundRun(
            bot,
            dispatch,
            request.get("prompt"),
            handle,
            request.get("images"),
            name=f"agentloop-pi-{self.round_id}",
        )
        self.runner.start()
        watcher = threading.Thread(
            target=self._watch_runner,
            args=(self.round_id, handle, self.runner),
            daemon=True,
            name=f"agentloop-pi-watch-{self.round_id}",
        )
        watcher.start()
        return self.snapshot()

    def action_status(self, _request):
        return self.snapshot()

    def action_wait(self, request):
        self._require_handle()
        after = request.get("after", 0)
        timeout = request.get("timeout", 10.0)
        if isinstance(after, bool) or not isinstance(after, int) or after < 0:
            raise ProtocolError("after must be a non-negative integer")
        if (isinstance(timeout, bool) or not isinstance(timeout, (int, float))
                or timeout < 0 or timeout > 60):
            raise ProtocolError("timeout must be between 0 and 60 seconds")
        deadline = time.monotonic() + timeout
        with self._events_condition:
            while True:
                event = next((item for item in self._events
                              if item["sequence"] > after), None)
                if event is not None:
                    return {"event": event, "dropped": self._dropped}
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    dropped = self._dropped
                    break
                self._events_condition.wait(remaining)
        # Never take the Handle lock while holding the event condition: callbacks
        # copy Handle state first and enqueue second, so the reverse order deadlocks.
        return {"event": None, "dropped": dropped, "snapshot": self.snapshot()}

    def action_pause(self, _request):
        handle = self._require_handle()
        accepted = handle.pause()
        return {"accepted": accepted, "snapshot": self.snapshot()}

    def action_resume(self, _request):
        handle = self._require_handle()
        accepted = handle.resume()
        return {"accepted": accepted, "snapshot": self.snapshot()}

    def action_end(self, request):
        handle = self._require_handle()
        reason = request.get("reason", "pi")
        if not isinstance(reason, str) or not reason:
            raise ProtocolError("reason must be a non-empty string")
        accepted = handle.end(reason=reason)
        return {"accepted": accepted, "snapshot": self.snapshot()}

    def action_edit(self, request):
        handle = self._require_handle()
        changes = request.get("set")
        if not isinstance(changes, dict) or not changes:
            raise ProtocolError("edit requires a non-empty set object")
        unknown = sorted(set(changes) - EDIT_FIELDS)
        if unknown:
            raise ProtocolError(f"fields are not remotely editable: {unknown}")
        expected_step = request.get("expected_step")
        expected_state = request.get("expected_state")
        with handle.edit():
            if expected_step is not None and handle.step != expected_step:
                raise ProtocolError(
                    f"stale edit: expected step {expected_step}, got {handle.step}")
            if expected_state is not None and handle.state != expected_state:
                raise ProtocolError(
                    f"stale edit: expected state {expected_state!r}, "
                    f"got {handle.state!r}")
            _validate_changes(changes)
            for name, value in changes.items():
                setattr(handle, name, deepcopy(value))
        return self.snapshot()

    def action_join(self, request):
        self._require_handle()
        timeout = request.get("timeout", 0)
        if (isinstance(timeout, bool) or not isinstance(timeout, (int, float))
                or timeout < 0 or timeout > 60):
            raise ProtocolError("timeout must be between 0 and 60 seconds")
        self.runner.thread.join(timeout)
        return {"joined": not self.runner.is_alive(), "snapshot": self.snapshot()}

    def action_shutdown(self, request):
        if self.handle is None:
            return {"joined": True, "snapshot": self.snapshot()}
        reason = request.get("reason", "pi_shutdown")
        if not isinstance(reason, str) or not reason:
            raise ProtocolError("reason must be a non-empty string")
        if not self.handle.done():
            self.handle.end(reason=reason)
        timeout = request.get("timeout", 2.0)
        if (isinstance(timeout, bool) or not isinstance(timeout, (int, float))
                or timeout < 0 or timeout > 60):
            raise ProtocolError("timeout must be between 0 and 60 seconds")
        self.runner.thread.join(timeout)
        return {"joined": not self.runner.is_alive(), "snapshot": self.snapshot()}

    def snapshot(self):
        handle = self.handle
        if handle is None:
            return {"active": False, "round_id": None, "state": "absent"}
        with handle.edit():
            data = {
                "active": bool(self.runner and self.runner.is_alive()),
                "round_id": self.round_id,
                "state": handle.state,
                "step": handle.step,
                "message": handle.message,
                "tool_calls": handle.tool_calls,
                "tool_results": handle.tool_results,
                "tool_log": handle.tool_log,
                "calls": handle.calls,
                "used": handle.used,
                "input_tokens": handle.input_tokens,
                "output_tokens": handle.output_tokens,
                "cached_input_tokens": handle.cached_input_tokens,
                "stop": handle.stop,
                "end_reason": handle.end_reason,
                "err": None if handle.err is None else (
                    f"{type(handle.err).__name__}: {handle.err}"),
                "auto_finish": handle.auto_finish,
            }
        return _jsonable(data)

    def close(self):
        if self.handle is not None and not self.handle.done():
            self.handle.end(reason="bridge_eof")

    def _require_handle(self):
        if self.handle is None:
            raise ProtocolError("no Round has been started")
        return self.handle

    def _on_boundary(self, boundary, handle, gate):
        self._append_event(boundary, self._snapshot_for(handle), gate=gate)
        return agentloop.PAUSE if gate == "pause" else agentloop.CONTINUE

    def _watch_runner(self, round_id, handle, runner):
        runner.thread.join()
        if self.handle is handle and self.round_id == round_id:
            self._append_event("finished", self._snapshot_for(handle))

    def _snapshot_for(self, handle):
        if handle is self.handle:
            return self.snapshot()
        return {"active": False, "state": "replaced"}

    def _append_event(self, boundary, snapshot, gate=None):
        with self._events_condition:
            self._sequence += 1
            if len(self._events) == self._events.maxlen:
                self._dropped += 1
            event = {
                "sequence": self._sequence,
                "round_id": self.round_id,
                "boundary": boundary,
                "snapshot": snapshot,
            }
            if gate is not None:
                event["gate"] = gate
            self._events.append(event)
            self._events_condition.notify_all()


def _load_factory(spec):
    if not isinstance(spec, str) or ":" not in spec:
        raise ProtocolError("factory must use module:function syntax")
    module_name, function_name = spec.rsplit(":", 1)
    if not module_name or not function_name:
        raise ProtocolError("factory must use module:function syntax")
    try:
        module = importlib.import_module(module_name)
        factory = getattr(module, function_name)
    except (ImportError, AttributeError) as err:
        raise ProtocolError(f"cannot load factory {spec!r}: {err}") from err
    if not callable(factory):
        raise ProtocolError(f"factory {spec!r} is not callable")
    return factory


def _accepts_argument(factory):
    try:
        inspect.signature(factory).bind({})
    except (TypeError, ValueError):
        return False
    return True


def _gate(name, value):
    if value not in {"continue", "pause"}:
        raise ProtocolError(f"{name} gate must be 'continue' or 'pause'")
    return value


def _validate_changes(changes):
    if "prompt" in changes and not (
            changes["prompt"] is None or isinstance(changes["prompt"], str)):
        raise ProtocolError("prompt must be a string or null")
    if "images" in changes:
        images = changes["images"]
        if not (images is None or (
                isinstance(images, list)
                and all(isinstance(item, str) for item in images))):
            raise ProtocolError("images must be an array of strings or null")
    if "auto_finish" in changes and not isinstance(changes["auto_finish"], bool):
        raise ProtocolError("auto_finish must be a boolean")
    for name in ("ask_options", "tool_results"):
        if name in changes and not isinstance(changes[name], dict):
            raise ProtocolError(f"{name} must be an object")
    if "tool_calls" in changes and not isinstance(changes["tool_calls"], list):
        raise ProtocolError("tool_calls must be an array")


def _jsonable(value, depth=0):
    if depth > 8:
        return "<maximum depth reached>"
    if value is None or isinstance(value, (bool, int, float)):
        return value
    if isinstance(value, str):
        if len(value) <= MAX_STRING:
            return value
        return value[:MAX_STRING] + f"... <truncated {len(value) - MAX_STRING} chars>"
    if isinstance(value, dict):
        items = list(value.items())[:100]
        out = {str(key): _jsonable(item, depth + 1) for key, item in items}
        if len(value) > 100:
            out["<truncated>"] = f"{len(value) - 100} more entries"
        return out
    if isinstance(value, (list, tuple)):
        out = [_jsonable(item, depth + 1) for item in value[:100]]
        if len(value) > 100:
            out.append(f"<truncated {len(value) - 100} more items>")
        return out
    return repr(value)[:MAX_STRING]


def _response(request_id, result=None, error=None):
    if error is None:
        return {"id": request_id, "ok": True, "result": result}
    return {
        "id": request_id,
        "ok": False,
        "error": {"type": type(error).__name__, "message": str(error)},
    }


def serve(factory_spec=None, stdin=None, stdout=None):
    source = stdin or sys.stdin
    protocol_out = stdout or sys.stdout
    bridge = Bridge(factory_spec)
    for raw in source:
        request_id = None
        try:
            request = json.loads(raw)
            request_id = request.get("id") if isinstance(request, dict) else None
            result = bridge.dispatch(request)
            response = _response(request_id, result=result)
        except Exception as err:
            request_id = locals().get("request_id")
            response = _response(request_id, error=err)
        protocol_out.write(json.dumps(response, ensure_ascii=False) + "\n")
        protocol_out.flush()
    bridge.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--factory",
        default=os.environ.get("AGENTLOOP_PI_FACTORY"),
        help="trusted module:function returning (bot, dispatch)",
    )
    args = parser.parse_args(argv)
    protocol_out = sys.stdout
    # Factory, bot and tool prints must not corrupt stdout JSONL framing.
    sys.stdout = sys.stderr
    serve(args.factory, stdout=protocol_out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
