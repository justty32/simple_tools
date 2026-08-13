"""Small helpers for human-facing FreePy shells."""

from typing import NamedTuple


class Assistant(NamedTuple):
    """A bot and its matching tool dispatch, ready to start local sessions.

    The tuple shape keeps existing ``bot, dispatch = assistant(...)`` code
    working while :meth:`session` removes that unpacking boilerplate in a REPL.
    """

    bot: object
    dispatch: dict

    def session(self, prompt=None, *, handle=None, images=None, daemon=False,
                name=None):
        """Start an interactive Round using this matched bot and dispatch."""
        return session(
            self.bot, self.dispatch, prompt, handle=handle, images=images,
            daemon=daemon, name=name)


def toolbox(*sources):
    """Combine explicit Python callables and ``(schemas, dispatch)`` bundles.

    Sources are never discovered implicitly. Duplicate tool names are rejected
    instead of silently changing which effect a model call will execute.
    """
    from llms import normalize_tools

    return normalize_tools(sources)


def assistant(engine=None, *tool_sources, system=None):
    """Build an LLM bot and its matching dispatch for an interactive session.

    ``engine`` may be an Engine, a preset id, or ``None`` for llms defaults.
    Tool sources use :func:`toolbox` and therefore remain explicit.
    """
    from llms import Bot, LLM, load_preset

    if isinstance(engine, str):
        engine = load_preset(engine)
    elif engine is not None and not isinstance(engine, LLM):
        raise TypeError("engine must be an LLM/Engine, preset id, or None")
    bot = Bot(engine, system=system, tools=tool_sources)
    return Assistant(bot, bot.dispatch)


def session(bot, dispatch=None, prompt=None, *, handle=None, images=None,
            daemon=False, name=None):
    """Start one interactive local Round and return its Controller.

    A new Handle waits for more input after a natural answer. Supplying a
    Handle keeps all of that Handle's explicit settings unchanged.
    """
    from agentloop import Controller, Handle

    if handle is None:
        handle = Handle(auto_finish=False)
    return Controller(
        bot, dispatch, prompt, handle=handle, images=images
    ).start(daemon=daemon, name=name)


__all__ = ["Assistant", "assistant", "session", "toolbox"]
