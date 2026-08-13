"""Normalize complete model-visible and executable tool capabilities."""

from .func_schema import to_tools


def normalize_tools(tools):
    """Return a validated ``(schemas, dispatch)`` pair.

    Accept one callable, an iterable of callables, or an existing pair. Tool
    names must be unique and schemas must exactly match executable dispatch.
    """
    if tools is None:
        return [], {}
    if callable(tools):
        sources = [tools]
    elif (isinstance(tools, tuple) and len(tools) == 2
          and isinstance(tools[1], dict)):
        sources = [tools]
    else:
        try:
            sources = list(tools)
        except TypeError:
            raise TypeError(
                "tools must be a callable, callable iterable, or "
                "(schemas, dispatch) pair") from None

    schemas = []
    dispatch = {}
    for source in sources:
        group_schemas, group_dispatch = _one_source(source)
        names = _names(group_schemas)
        if len(names) != len(set(names)):
            raise ValueError(f"duplicate tool names in one source: {names}")
        if set(names) != set(group_dispatch):
            raise ValueError(
                "tool schemas and dispatch must contain the same names: "
                f"schemas={names}, dispatch={list(group_dispatch)}")
        if not all(callable(fn) for fn in group_dispatch.values()):
            raise ValueError("every tool dispatch value must be callable")
        duplicates = set(dispatch) & set(group_dispatch)
        if duplicates:
            raise ValueError(f"duplicate tool names: {sorted(duplicates)}")
        schemas.extend(group_schemas)
        dispatch.update(group_dispatch)
    return schemas, dispatch


def _one_source(source):
    if callable(source):
        return to_tools(source)
    if (isinstance(source, tuple) and len(source) == 2
            and isinstance(source[1], dict)):
        try:
            return list(source[0]), dict(source[1])
        except TypeError:
            pass
    raise TypeError("each tool must be callable or a (schemas, dispatch) pair")


def _names(schemas):
    try:
        return [schema["function"]["name"] for schema in schemas]
    except (TypeError, KeyError):
        raise ValueError("tool schemas must contain function names") from None
