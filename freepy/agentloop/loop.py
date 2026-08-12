"""The small synchronous driver and one-safe-boundary runner operation."""

from .calling import perform
from .handle import Handle, _RunnerContractError


def advance(bot=None, dispatch=None, prompt=None, handle=None, images=None):
    """Advance a Round by one Step or one complete tool batch, without parking.

    On the first call provide ``bot`` (and optional initial inputs); later calls
    need only the same ``handle``.  A paused/waiting/completed Handle is returned
    unchanged.  This is also the runner operation used by ``Controller``.
    """
    h = handle or Handle()
    if h.state == "idle":
        if bot is None:
            raise ValueError("the first advance() needs a bot")
        table = dict(h.dispatch)
        table.update(dispatch or {})
        h._begin(bot, table, prompt, images)
    elif any(value is not None for value in (bot, dispatch, prompt, images)):
        raise ValueError("later advance() calls take only handle=")

    if h.done():
        return h
    try:
        started = h._start_next()
    except _RunnerContractError:
        raise
    except Exception as err:
        return h._fail(err)
    if started is None:
        return h
    action, payload = started
    try:
        if action == "tools":
            calls, current_dispatch, seed = payload
            results, records = _perform_all(calls, current_dispatch, seed)
            h._commit_tools(calls, results, records)
            return h

        step_prompt, step_images, results, options = payload
        reply = h.bot.ask(prompt=step_prompt, images=step_images,
                          tool_results=results or None, **options)
        text = getattr(reply, "text", "")
        calls = getattr(reply, "calls", [])
        finish_reason = getattr(reply, "finish_reason", None)
        committed = bool(text or calls or finish_reason is not None)
        if not committed:
            err = getattr(reply, "err", None) or RuntimeError(
                "bot.ask() returned no message")
            return h._fail(err)
        h._commit_step(reply, text, calls, finish_reason,
                       getattr(reply, "usage", None))
        if getattr(reply, "err", None) is not None:
            return h._fail(reply.err)
        return h
    except Exception as err:
        return h._fail(err)


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
    while not h.done():
        advance(handle=h)
        if h.done():
            break
        if h.state in {"waiting", "paused"} and not h._wait_until_ready():
            break
    return h


def _perform_all(calls, dispatch, seed):
    """Execute one mutable, already-approved tool batch in order."""
    results = dict(seed)
    records = []
    for call in calls:
        out = perform(dispatch, call)
        results[call["id"]] = out
        records.append((call, out, True))
    return results, records
