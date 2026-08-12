"""Parse the ``_type: http`` execution recipe and register it with tooljson."""

from urllib.parse import urlsplit

from tooljson import register
from tooljson.spec import need

METHODS = {"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE"}
RESERVED_HEADERS = {
    "host", "content-length", "content-type", "transfer-encoding", "accept-encoding",
}


def _mapping(extra, key, props):
    value = extra.get(key) or {}
    need(isinstance(value, dict), f"_extra.{key} must be an object")
    need(all(isinstance(k, str) and isinstance(v, str) and v for k, v in value.items()),
         f"_extra.{key} must map argument names to non-empty wire names")
    unknown = sorted(set(value) - set(props))
    need(not unknown, f"_extra.{key} maps unknown argument(s): {', '.join(unknown)}")
    names = list(value.values())
    need(len(names) == len(set(names)), f"_extra.{key} contains duplicate wire names")
    return value


def _headers(extra):
    headers = extra.get("headers") or {}
    need(isinstance(headers, dict) and all(
        isinstance(k, str) and k and isinstance(v, str) for k, v in headers.items()
    ), "_extra.headers must be an object of string names and values")
    lowered = [name.lower() for name in headers]
    need(len(lowered) == len(set(lowered)), "_extra.headers contains duplicate names")
    for name, header_value in headers.items():
        need("\r" not in name + header_value and "\n" not in name + header_value,
             "_extra.headers cannot contain newlines")
        need(name.lower() not in RESERVED_HEADERS,
             f"_extra.headers cannot set reserved header {name!r}")
    return headers


class HttpBody:
    """A fixed HTTP endpoint plus explicit argument-to-wire mappings."""

    def __init__(self, spec):
        self.spec, extra = spec, spec.extra
        self.url = extra.get("url")
        parsed = urlsplit(self.url) if isinstance(self.url, str) else None
        need(parsed is not None and parsed.scheme in ("http", "https") and parsed.netloc,
             "_extra.url must be an absolute http or https URL")
        need(not parsed.username and not parsed.password,
             "_extra.url cannot contain credentials")
        need(not parsed.query and not parsed.fragment,
             "_extra.url cannot contain query or fragment; use _extra.query")
        need(len(self.url.encode("utf-8")) <= 8192, "_extra.url is over the 8192 byte limit")

        method = extra.get("method", "GET")
        need(isinstance(method, str) and method.upper() in METHODS,
             f"_extra.method must be one of {sorted(METHODS)}")
        self.method = method.upper()
        self.query = _mapping(extra, "query", spec.props)
        self.json = _mapping(extra, "json", spec.props)
        overlap = sorted(set(self.query) & set(self.json))
        need(not overlap, f"argument(s) mapped twice: {', '.join(overlap)}")
        unmapped = sorted(set(spec.props) - set(self.query) - set(self.json))
        need(not unmapped, f"argument(s) have no HTTP mapping: {', '.join(unmapped)}")
        need(self.method not in ("GET", "HEAD") or not self.json,
             f"{self.method} tools cannot send a JSON body")
        need(set(spec.required) <= set(spec.props), "required contains an unknown argument")

        self.headers = _headers(extra)
        self.timeout = extra.get("timeout", 30)
        need(isinstance(self.timeout, (int, float)) and not isinstance(self.timeout, bool)
             and 0 < self.timeout <= 300, "_extra.timeout must be between 0 and 300 seconds")
        self.ok_status = extra.get("ok_status", list(range(200, 300)))
        need(isinstance(self.ok_status, list) and self.ok_status and all(
            isinstance(code, int) and 100 <= code <= 599 for code in self.ok_status
        ), "_extra.ok_status must be a non-empty list of HTTP status integers")
        self.max_response_bytes = extra.get("max_response_bytes", 30000)
        need(isinstance(self.max_response_bytes, int)
             and 1 <= self.max_response_bytes <= 4 * 1024 * 1024,
             "_extra.max_response_bytes must be between 1 and 4194304")
        self.limits = extra.get("limits") or {}
        need(isinstance(self.limits, dict), "_extra.limits must be an object")
        unknown_limits = sorted(set(self.limits) - set(spec.props))
        need(not unknown_limits,
             f"_extra.limits contains unknown argument(s): {', '.join(unknown_limits)}")
        for name, rule in self.limits.items():
            need(isinstance(rule, dict), f"_extra.limits[{name!r}] must be an object")
            unknown = sorted(set(rule) - {"max_bytes", "min", "max"})
            need(not unknown, f"_extra.limits[{name!r}] has unknown keys: {unknown}")
            cap = rule.get("max_bytes")
            need(cap is None or (isinstance(cap, int) and cap >= 0),
                 f"_extra.limits[{name!r}].max_bytes must be a non-negative integer")
            low, high = rule.get("min"), rule.get("max")
            need(low is None or (isinstance(low, (int, float)) and not isinstance(low, bool)),
                 f"_extra.limits[{name!r}].min must be a number")
            need(high is None or (isinstance(high, (int, float)) and not isinstance(high, bool)),
                 f"_extra.limits[{name!r}].max must be a number")
            need(low is None or high is None or low <= high,
                 f"_extra.limits[{name!r}] has min greater than max")

    @property
    def target(self):
        """HTTP endpoints have no local file for ``Spec.stale`` to fingerprint."""
        return None

    def run(self, arguments) -> str:
        from .invoke import run_http
        return run_http(self, arguments)


register("http", HttpBody)
