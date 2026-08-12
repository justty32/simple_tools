"""Build and execute one bounded request for an ``_type: http`` tool."""

import json
import socket
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import HTTPRedirectHandler, Request, build_opener

from .arguments import query_value, validate


class _NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


_OPENER = build_opener(_NoRedirect)
_approver = None


def set_approver(fn):
    """Set ``fn(name, method, url, arguments) -> bool``; ``None`` allows all."""
    global _approver
    _approver = fn


def _request(body, args):
    pairs = [(wire, query_value(args[name])) for name, wire in body.query.items() if name in args]
    query = urlencode(pairs)
    if len(query.encode()) > 8192:
        return None, "Error: encoded query is over the 8192 byte limit"
    url = body.url + ("?" + query if query else "")
    payload = {wire: args[name] for name, wire in body.json.items() if name in args}
    data = None
    headers = {"Accept": "application/json, text/plain", "Accept-Encoding": "identity",
               "User-Agent": "FreePy-http_tools/0"}
    headers.update(body.headers)
    if body.json:
        data = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode()
        if len(data) > 65536:
            return None, "Error: encoded JSON body is over the 65536 byte limit"
        headers["Content-Type"] = "application/json; charset=utf-8"
    return Request(url, data=data, headers=headers, method=body.method), None


def _read(response, cap):
    raw = response.read(cap + 1)
    clipped = len(raw) > cap
    raw = raw[:cap]
    if b"\x00" in raw:
        return f"(binary response, at least {len(raw)} bytes, not shown)"
    charset = response.headers.get_content_charset() or "utf-8"
    text = raw.decode(charset, errors="replace").strip()
    if clipped:
        text += f"\n… [response truncated after {cap} bytes]"
    return text


def run_http(body, arguments):
    """Execute once; runtime failures are strings suitable for a tool message."""
    try:
        args, error = validate(body, arguments)
        if error:
            return error
        request, error = _request(body, args)
        if error:
            return error
        if _approver is not None:
            try:
                allowed = _approver(body.spec.name, body.method, request.full_url, dict(args))
            except Exception as exc:
                return f"Error: approval check failed: {exc}"
            if not allowed:
                return "Error: the user declined to send this HTTP request"
        try:
            response = _OPENER.open(request, timeout=body.timeout)
        except HTTPError as exc:
            response = exc
        with response:
            status = response.status
            text = _read(response, body.max_response_bytes)
    except (URLError, socket.timeout, TimeoutError) as exc:
        return f"Error: HTTP request failed: {exc}"
    except Exception as exc:
        return f"Error: HTTP request failed: {type(exc).__name__}: {exc}"
    if status not in body.ok_status:
        return f"HTTP {status}\n{text}" if text else f"HTTP {status} (no output)"
    return text or f"(no output, HTTP {status})"
