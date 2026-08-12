"""Validate model arguments and render values for an HTTP request."""

import json

BAD = object()


def _coerce(value, kind):
    if not isinstance(value, str):
        return value
    try:
        if kind == "integer":
            return int(value.strip())
        if kind == "number":
            return float(value.strip())
    except ValueError:
        return BAD
    if kind == "boolean" and value.strip().lower() in ("true", "false"):
        return value.strip().lower() == "true"
    return value


def _matches(value, kind):
    if kind == "string":
        return isinstance(value, str)
    if kind == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if kind == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if kind == "boolean":
        return isinstance(value, bool)
    if kind == "array":
        return isinstance(value, list)
    if kind == "object":
        return isinstance(value, dict)
    return True


def _limit(name, value, rule):
    if value is None or not isinstance(rule, dict):
        return None
    cap = rule.get("max_bytes")
    if cap is not None and isinstance(value, str) and len(value.encode()) > cap:
        return f"Error: argument {name!r} is over the {cap} byte limit"
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if rule.get("min") is not None and value < rule["min"]:
            return f"Error: argument {name!r} is below the minimum {rule['min']}"
        if rule.get("max") is not None and value > rule["max"]:
            return f"Error: argument {name!r} is above the maximum {rule['max']}"
    return None


def validate(body, arguments):
    """Return ``(coerced_args, error)`` without performing network I/O."""
    if not isinstance(arguments, dict):
        return None, f"Error: arguments must be a JSON object, got {type(arguments).__name__}"
    if not all(isinstance(name, str) for name in arguments):
        return None, "Error: argument names must be strings"
    unknown = sorted(set(arguments) - set(body.spec.props))
    if unknown:
        return None, f"Error: unknown argument(s): {', '.join(unknown)}"
    args = {name: _coerce(value, (body.spec.props[name] or {}).get("type"))
            for name, value in arguments.items() if value is not None}
    wrong = sorted(name for name, value in args.items() if value is BAD or not _matches(
        value, (body.spec.props[name] or {}).get("type")
    ))
    if wrong:
        return None, f"Error: argument(s) {', '.join(wrong)} have the wrong type"
    missing = [name for name in body.spec.required if name not in args]
    if missing:
        return None, f"Error: missing required argument(s): {', '.join(missing)}"
    for name, rule in body.limits.items():
        error = _limit(name, args.get(name), rule)
        if error:
            return None, error
    return args, None


def query_value(value):
    """Render structured JSON values deterministically before URL encoding."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    return str(value)
