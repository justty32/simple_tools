"""toolcalls.py — 把 tool_calls 在三種形狀之間搬。

    raw      [(id, name, arguments_字串)]，內部用的中間形狀
    history  塞回 history 的 assistant message（要長得跟 API 收的一模一樣）
    entries  給呼叫端用的 [{"id", "name", "args"}]

串流過來的是一片一片的碎片，用 Accumulator 拼回 raw。
"""

import json


def _entry(call_id, name, arguments):
    """組出給呼叫端用的一筆 call；arguments 不是合法 JSON 就給空 dict 並附上 args_raw。"""
    entry = {"id": call_id, "name": name}
    try:
        entry["args"] = json.loads(arguments)
    except (json.JSONDecodeError, TypeError):
        entry["args"] = {}
        entry["args_raw"] = arguments
    return entry


def entries(raw_calls) -> list:
    """raw -> [{"id", "name", "args"}]。"""
    return [_entry(*item) for item in raw_calls]


def raw_from(msg) -> list:
    """非串流回應的 message -> raw。沒有 tool_calls 就是空 list。"""
    return [
        (tc.id, tc.function.name, tc.function.arguments)
        for tc in (msg.tool_calls or [])
    ]


def history_message(content, raw_calls) -> dict:
    """組出 API 認得的 assistant message；raw_calls 是空的就是單純一句話。"""
    if not raw_calls:
        return {"role": "assistant", "content": content}
    return {
        "role": "assistant",
        "content": content or None,  # 只叫工具沒說話時，content 要是 null 不是空字串
        "tool_calls": [
            {
                "id": call_id,
                "type": "function",
                "function": {"name": name, "arguments": arguments},
            }
            for call_id, name, arguments in raw_calls
        ],
    }


def result_messages(tool_results: dict) -> list:
    """把 {call_id: 執行結果} 轉成一串 role="tool" 的訊息。"""
    return [
        {"role": "tool", "tool_call_id": call_id, "content": str(result)}
        for call_id, result in tool_results.items()
    ]


class Accumulator:
    """把串流回來的 tool_call 碎片依 index 拼回 raw。

    串流時 id / name 通常只在第一片出現，arguments 則是一小段一小段接起來的。
    """

    def __init__(self):
        self._parts = {}  # index -> [id, name, arguments]

    def feed(self, deltas):
        """吃一個 chunk 裡的 delta.tool_calls（沒有就傳 None）。"""
        for delta in deltas or []:
            slot = self._parts.setdefault(getattr(delta, "index", 0), ["", "", ""])
            if delta.id:
                slot[0] = delta.id
            fn = getattr(delta, "function", None)
            if fn is None:
                continue
            if fn.name:
                slot[1] = fn.name
            if fn.arguments:
                slot[2] += fn.arguments

    def __bool__(self):
        return bool(self._parts)

    def raw(self) -> list:
        """[(id, name, arguments_字串)]，依 index 排好。"""
        return [tuple(self._parts[i]) for i in sorted(self._parts)]
