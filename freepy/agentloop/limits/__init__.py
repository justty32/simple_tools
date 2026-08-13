"""Built-in callback policies for bounding an agentloop Round."""

import math
from collections.abc import Mapping

from ..handle import CONTINUE, END


class Limits:
    """The built-in convenience policy, not part of the loop's control core."""

    def __init__(self, steps=12, calls=None, per_tool=None, tools=None,
                 engines=None, seconds=None, input_tokens=None,
                 output_tokens=None):
        _positive_int("steps", steps)
        _optional_count("calls", calls)
        _optional_seconds(seconds)
        _optional_count("input_tokens", input_tokens)
        _optional_count("output_tokens", output_tokens)
        if per_tool is not None and not isinstance(per_tool, Mapping):
            raise ValueError(f"per_tool must be a mapping, got {per_tool!r}")
        caps = dict(per_tool or {})
        for name, cap in caps.items():
            _name("per_tool key", name)
            _optional_count(f"per_tool[{name!r}]", cap, optional=False)

        self.steps = steps
        self.calls = calls
        self.per_tool = caps
        self.tools = _names("tools", tools)
        self.engines = _names("engines", engines)
        self.seconds = seconds
        self.input_tokens = input_tokens
        self.output_tokens = output_tokens

    def attach(self, handle):
        """Register this policy through the public callback list."""
        handle.after_step.append(self.after_step)
        return self

    def after_step(self, handle):
        """Apply budgets and edit pending calls before tools start."""
        wrong_engine = self._engine_error(handle.bot)
        if wrong_engine:
            handle.end_reason = "engine"
            handle.err = ValueError(wrong_engine)
            return END
        if self.seconds is not None and handle.elapsed() >= self.seconds:
            handle.end_reason = "time"
            return END
        if (self.input_tokens is not None
                and handle.input_tokens >= self.input_tokens):
            handle.end_reason = "input_tokens"
            return END
        if (self.output_tokens is not None
                and handle.output_tokens >= self.output_tokens):
            handle.end_reason = "output_tokens"
            return END
        if handle.tool_calls and handle.step >= self.steps:
            handle.end_reason = "budget"
            return END
        if handle.tool_calls and self.calls is not None and handle.calls >= self.calls:
            handle.end_reason = "calls"
            return END

        allowed = []
        reserved = dict(handle.tool_results)
        remaining = None if self.calls is None else max(0, self.calls - handle.calls)
        planned = {}
        for call in handle.tool_calls:
            name = call.get("name")
            blocked = self._blocked(name, handle, planned, remaining, len(allowed))
            if blocked:
                reserved[call["id"]] = blocked
            else:
                allowed.append(call)
                planned[name] = planned.get(name, 0) + 1
        handle.tool_calls = allowed
        handle.tool_results = reserved
        return CONTINUE

    def _blocked(self, name, handle, planned, remaining, accepted):
        if self.tools is not None and name not in self.tools:
            names = ", ".join(sorted(self.tools)) or "(none)"
            return (f"Error: {name} is not available for this task. "
                    f"You may only use: {names}")
        cap = self.per_tool.get(name)
        already = handle.used.get(name, 0) + planned.get(name, 0)
        if cap is not None and already >= cap:
            return (f"Error: {name} has already been used {cap} times, which is its limit "
                    "for this task. Finish with the other tools.")
        if remaining is not None and accepted >= remaining:
            return (f"Error: the tool budget for this task ({self.calls} calls) is used up. "
                    "Answer with what you already know.")
        return None

    def _engine_error(self, bot):
        if self.engines is None:
            return None
        llm = getattr(bot, "llm", None) or getattr(bot, "engine", None)
        model = getattr(llm, "model", None)
        if not model:
            return "問不出這個 bot 在用哪顆引擎，engines 這條限制沒辦法成立"
        if model not in self.engines:
            return f"引擎 {model} 不在准用的清單裡：{sorted(self.engines)}"
        return None

    def __repr__(self):
        on = {k: v for k, v in vars(self).items() if v is not None and v != {}}
        return f"Limits({', '.join(f'{k}={v!r}' for k, v in on.items())})"


def _positive_int(name, value):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(f"{name} must be a positive integer, got {value!r}")


def _optional_count(name, value, optional=True):
    if optional and value is None:
        return
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{name} must be a non-negative integer, got {value!r}")


def _optional_seconds(value):
    if value is None:
        return
    if (isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) or value < 0):
        raise ValueError(f"seconds must be a finite non-negative number, got {value!r}")


def _name(label, value):
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a non-empty string, got {value!r}")


def _names(label, values):
    if values is None:
        return None
    if isinstance(values, (str, bytes)):
        raise ValueError(f"{label} must be an iterable of names, not {values!r}")
    try:
        names = set(values)
    except (TypeError, ValueError) as err:
        raise ValueError(f"{label} must be an iterable of names, got {values!r}") from err
    for value in names:
        _name(f"{label} item", value)
    return names


__all__ = ["Limits"]
