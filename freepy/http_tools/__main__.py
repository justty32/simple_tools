"""Offline end-to-end checks using a temporary loopback HTTP server."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
from threading import Thread

import exec_tools
import tooljson

from . import set_approver, tools


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    hits = []

    def log_message(self, *args):
        pass

    def _reply(self, status, body=b"", headers=None):
        self.send_response(status)
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        type(self).hits.append(self.path)
        if self.path == "/redirect":
            self._reply(302, headers={"Location": "/should-not-run"})
        elif self.path == "/large":
            self._reply(200, b"abcdefghij")
        elif self.path.startswith("/echo"):
            self._reply(200, json.dumps({"path": self.path}).encode())
        else:
            self._reply(404, b'{"error":"missing"}')

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        payload = self.rfile.read(size)
        result = {"path": self.path, "body": json.loads(payload),
                  "language": self.headers.get("Accept-Language")}
        self._reply(201, json.dumps(result, ensure_ascii=False).encode())


def spec(name, url, method="GET", query=None, body=None, **extra):
    query, body = query or {}, body or {}
    props = {key: {"type": "integer" if key == "count" else "string"}
             for key in set(query) | set(body)}
    return {
        "type": "function",
        "function": {"name": name, "description": name, "parameters": {
            "type": "object", "properties": props, "required": list(props),
        }},
        "_extra": {"_version": "0.1.0", "_type": "http", "url": url,
                   "method": method, "query": query, "json": body, **extra},
    }


def check(condition, label):
    if not condition:
        raise AssertionError(label)
    print(f"ok  {label}")


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    root = f"http://127.0.0.1:{server.server_port}"
    try:
        with tempfile.TemporaryDirectory() as holder:
            path = Path(holder) / ".specs" / "http.json"
            entries = [
                spec("search", root + "/echo", query={"term": "q", "count": "n"}),
                spec("create", root + "/items", "POST", body={"term": "name"},
                     headers={"Accept-Language": "zh-TW"}, ok_status=[201]),
                spec("redirect", root + "/redirect"),
                spec("large", root + "/large", max_response_bytes=5),
                spec("missing", root + "/missing"),
            ]
            tooljson.save(entries, path)
            schemas, dispatch = tools(path)
            check(len(schemas) == len(dispatch) == 5, "registered type loads through tooljson")
            catalog_schemas, catalog_dispatch = exec_tools.tools([holder])
            check(len(catalog_schemas) == len(catalog_dispatch) == 5,
                  "registered type loads through exec_tools catalog")

            out = json.loads(dispatch["search"](term="a b", count="2"))
            check(out["path"] == "/echo?q=a+b&n=2", "query mapping, escaping, and coercion")
            out = json.loads(dispatch["create"](term="台北"))
            check(out == {"path": "/items", "body": {"name": "台北"}, "language": "zh-TW"},
                  "JSON body, UTF-8, static header, and custom success")
            check(dispatch["search"](term="x").startswith("Error: missing required"),
                  "missing argument is returned to model")
            check(dispatch["search"](term="x", count=1, extra=2).startswith("Error: unknown"),
                  "unknown argument is not dropped")
            check(dispatch["search"](term="x", count="nope").endswith("wrong type"),
                  "wrong scalar type is not sent")
            before = len(Handler.hits)
            set_approver(lambda *args: False)
            check(dispatch["search"](term="x", count=1).startswith("Error: the user declined"),
                  "approver can deny a request")
            check(len(Handler.hits) == before, "denied request never reaches the server")
            set_approver(None)

            before = len(Handler.hits)
            check(dispatch["redirect"]() == "HTTP 302 (no output)", "redirect is visible and blocked")
            check(len(Handler.hits) == before + 1 and Handler.hits[-1] == "/redirect",
                  "redirect target was not requested")
            check(dispatch["large"]() == "abcde\n… [response truncated after 5 bytes]",
                  "response byte bound is explicit")
            check(dispatch["missing"]() == 'HTTP 404\n{"error":"missing"}',
                  "HTTP error includes bounded body")

            broken = spec("broken", root + "/echo", query={"term": "q"})
            broken["_extra"]["url"] += "?hidden=1"
            try:
                tooljson.Spec(broken)
            except tooljson.SpecError as exc:
                check("cannot contain query" in str(exc), "unsafe URL shape fails at load time")
            else:
                raise AssertionError("query in fixed URL should be rejected")
    finally:
        set_approver(None)
        server.shutdown()
        server.server_close()
        thread.join(2)
    print("http_tools: all checks passed")


if __name__ == "__main__":
    main()
