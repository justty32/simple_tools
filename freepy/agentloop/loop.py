"""The small synchronous driver: Step, callbacks, tools, callbacks, repeat."""

from .calling import perform
from .handle import Handle


def run(bot, dispatch=None, prompt=None, handle=None, images=None):
    """Run one Round on the calling thread and return its Handle when it ends.

    ``waiting`` and ``paused`` park this same thread. A controller using the Handle
    may inspect/mutate public state and call ``resume()``. Thread creation and worker
    management deliberately belong to the caller.
    """
    h = handle or Handle()
    table = dict(h.dispatch)
    table.update(dispatch or {})
    h._begin(bot, table, prompt, images)

    try:
        action = "tools" if h.tool_calls else "step"
        while True:
            if action == "tools":
                started = h._start_tools()
                if started is None:
                    return h
                calls, current_dispatch, seed = started
                results, records = _perform_all(calls, current_dispatch, seed)
                if not h._commit_tools(calls, results, records):
                    return h

            started = h._start_step()
            if started is None:
                return h
            step_prompt, step_images, results, options = started
            reply = bot.ask(prompt=step_prompt, images=step_images,
                            tool_results=results or None, **options)
            text = getattr(reply, "text", "")
            calls = getattr(reply, "calls", [])
            finish_reason = getattr(reply, "finish_reason", None)
            committed = bool(text or calls or finish_reason is not None)
            if not committed:
                err = getattr(reply, "err", None) or RuntimeError(
                    "bot.ask() returned no message")
                return h._fail(err)
            action = h._commit_step(
                reply, text, calls, finish_reason, getattr(reply, "usage", None))
            if getattr(reply, "err", None) is not None:
                return h._fail(reply.err)
            if action == "end":
                return h
    except Exception as err:
        return h._fail(err)


def _perform_all(calls, dispatch, seed):
    """Execute one mutable, already-approved tool batch in order."""
    results = dict(seed)
    records = []
    for call in calls:
        out = perform(dispatch, call)
        results[call["id"]] = out
        records.append((call, out, True))
    return results, records
