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
    from llms import to_tools

    schemas = []
    dispatch = {}
    for source in sources:
        group_schemas, group_dispatch = (
            to_tools(source) if callable(source) else source
        )
        names = [schema["function"]["name"] for schema in group_schemas]
        if len(names) != len(set(names)):
            raise ValueError(f"duplicate tool names in one source: {names}")
        if set(names) != set(group_dispatch):
            raise ValueError(
                "tool schemas and dispatch must contain the same names: "
                f"schemas={names}, dispatch={list(group_dispatch)}"
            )
        duplicates = set(dispatch) & set(group_dispatch)
        if duplicates:
            raise ValueError(f"duplicate tool names: {sorted(duplicates)}")
        schemas.extend(group_schemas)
        dispatch.update(group_dispatch)
    return schemas, dispatch


def assistant(engine=None, *tool_sources, system=None):
    """Build an LLM bot and its matching dispatch for an interactive session.

    ``engine`` may be an Engine, a preset id, or ``None`` for llms defaults.
    Tool sources use :func:`toolbox` and therefore remain explicit.
    """
    from llms import Engine, LLM, load_preset

    if isinstance(engine, str):
        engine = load_preset(engine)
    elif engine is not None and not isinstance(engine, Engine):
        raise TypeError("engine must be an Engine, preset id, or None")
    schemas, dispatch = toolbox(*tool_sources)
    return Assistant(
        LLM(engine=engine, system=system, tools=schemas or None), dispatch)


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
