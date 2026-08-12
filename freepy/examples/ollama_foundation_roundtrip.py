"""Manual Ollama roundtrip through all current FreePy foundation effects."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import sys
import tempfile
import threading
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "llmkit")]

from _ollama_probe import manual_probe, progress  # noqa: E402
import base_tools  # noqa: E402
import exec_tools  # noqa: E402
import http_tools  # noqa: E402,F401 - registers the HTTP tooljson type
from agentloop import Controller, Handle  # noqa: E402
from agentloop.limits import Limits  # noqa: E402
from llms import Engine, LLM, Params, to_tools  # noqa: E402

EXPECTED = "project=cedar\nregion=TW\nunits=50\nshipping=7\ngrand=57\nstatus=verified"
LIMITS = {
    "max_tokens_per_step": 4096,
    "steps": 16,
    "calls": 16,
    "seconds": 600,
}


class Shipping(BaseHTTPRequestHandler):
    calls = []

    def do_GET(self):
        parsed = urlsplit(self.path)
        query = parse_qs(parsed.query)
        try:
            region = query["region"][0]
            total = int(query["total"][0])
        except (KeyError, ValueError):
            self.send_error(400)
            return
        shipping = 7 if region == "TW" and total >= 50 else 12
        self.calls.append({"region": region, "total": total})
        body = json.dumps({"region": region, "shipping": shipping}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format, *_args):
        pass


def write_json(path, value):
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def make_catalog(root, port):
    work, catalog = root / "work", root / "catalog"
    specs = catalog / ".specs"
    work.mkdir()
    specs.mkdir(parents=True)
    (work / "order.txt").write_text(
        "project=cedar\nregion=TW\nunits=17,25,8\n", encoding="utf-8")
    helper = catalog / "sum_numbers.py"
    helper.write_text(
        "import sys\nprint(sum(int(x.strip()) for x in "
        "sys.stdin.read().split(',') if x.strip()))\n", encoding="utf-8")
    common = {"type": "function"}
    write_json(specs / "sum.json", {**common, "function": {
        "name": "sum_numbers", "description": "加總逗號分隔的整數；不要自行心算。",
        "parameters": {"type": "object", "properties": {
            "values": {"type": "string"}}, "required": ["values"]}},
        "_extra": {"_version": "0.1.0", "_type": "exec",
                   "exec": [sys.executable.replace("\\", "/"), str(helper)],
                   "argv": {}, "stdin": {"param": "values"}, "timeout": 10}})
    write_json(specs / "shipping.json", {**common, "function": {
        "name": "quote_shipping", "description": "向固定的本機服務查詢運費。",
        "parameters": {"type": "object", "properties": {
            "region": {"type": "string"}, "total": {"type": "integer"}},
            "required": ["region", "total"]}},
        "_extra": {"_version": "0.1.0", "_type": "http", "method": "GET",
                   "url": f"http://127.0.0.1:{port}/shipping",
                   "query": {"region": "region", "total": "total"},
                   "limits": {"region": {"max_bytes": 20},
                              "total": {"min": 0, "max": 100000}}}})
    return work, catalog


def execute_round(host, model, work, catalog):
    base_tools.set_root(work)
    base_schema, base_dispatch = to_tools(
        base_tools.read_file, base_tools.write_file, base_tools.edit_file)
    found = exec_tools.scan([catalog])
    if found.errors or set(found.specs) != {"sum_numbers", "quote_shipping"}:
        raise RuntimeError(f"bad discovery result: {found}")
    effect_schema, effect_dispatch = exec_tools.tools([catalog])
    schemas = base_schema + effect_schema
    dispatch = {**base_dispatch, **effect_dispatch}
    engine = Engine(
        model, url=host + "/v1", key="ollama", timeout=300,
        params=Params(
            temperature=0, max_tokens=LIMITS["max_tokens_per_step"]),
        caps={"tools": True})
    bot = LLM(engine, tools=schemas, system=(
        "你是嚴格的整合測試 agent。不得自行計算或省略步驟；工具輸出才是事實。"))
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
    task = """讀 order.txt，依序完成：
1. 用 sum_numbers 計算 units，禁止心算。
2. 用 quote_shipping 查 region 與 units 總數的運費。
3. 用 write_file 寫 result.txt：project、region、units、shipping、grand、status=pending，每行 key=value。
   content 必須有 6 個實體行；使用真正的換行，禁止寫成反斜線與 n 兩個字元。
4. 用 edit_file 把 status=pending 改成 status=verified。
5. 用 read_file 讀回 result.txt 驗證，再簡短報告。不得跳步。"""
    result = Controller(bot, dispatch, task, handle=handle).run()
    result_path = work / "result.txt"
    actual = (result_path.read_text(encoding="utf-8")
              if result_path.is_file() else None)
    required = {
        "read_file", "write_file", "edit_file", "sum_numbers",
        "quote_shipping"}
    passed = (
        result.state == "completed"
        and result.err is None
        and actual == EXPECTED
        and required <= set(result.used)
        and Shipping.calls == [{"region": "TW", "total": 50}]
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
        "result_file": actual,
        "http_calls": Shipping.calls,
        "tool_log": result.tool_log,
        "answer": result.text,
        "round_error": None if result.err is None else repr(result.err),
    }


def run_round(host, model):
    Shipping.calls = []
    server = ThreadingHTTPServer(("127.0.0.1", 0), Shipping)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory() as tmp:
            work, catalog = make_catalog(Path(tmp), server.server_port)
            return execute_round(host, model, work, catalog)
    finally:
        server.shutdown()
        server.server_close()


def main(argv=None):
    return manual_probe(
        "Run all current FreePy foundation effects against Ollama.",
        LIMITS, run_round, argv)


if __name__ == "__main__":
    raise SystemExit(main())
