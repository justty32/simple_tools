"""func_schema.py — 把一般 python 函式轉成 OpenAI tool schema dict。

只做這一件事：靠 inspect.signature + type hints + docstring，組出
OpenAI function-calling 需要的 schema。不做驗證、不做執行、不做快取。

用法：
    schemas = to_schemas(get_weather)                 # 只要 schema
    schemas, dispatch = to_tools(get_weather)          # schema + name -> function 對照表
    # 收到 tool call 後：dispatch[call["name"]](**call["args"])
"""

import inspect

from .docstrings import param_descriptions, summary
from .jsontypes import json_type, literal_values, unwrap_optional


def _properties(sig, param_docs):
    """把 signature 掃成 (properties, required)；self/cls 和 *args/**kwargs 一律跳過。"""
    properties = {}
    required = []

    for pname, param in sig.parameters.items():
        if pname in ("self", "cls"):
            continue
        if param.kind in (
            inspect.Parameter.VAR_POSITIONAL,
            inspect.Parameter.VAR_KEYWORD,
        ):
            continue

        annotation = param.annotation
        if annotation is inspect.Parameter.empty:
            annotation = str

        inner, optional = unwrap_optional(annotation)

        prop = {"type": json_type(inner)}
        values = literal_values(inner)
        if values is not None:
            prop["enum"] = values

        desc = param_docs.get(pname)
        if desc:
            prop["description"] = desc

        properties[pname] = prop

        has_default = param.default is not inspect.Parameter.empty
        if not has_default and not optional:
            required.append(pname)

    return properties, required


def to_schema(fn) -> dict:
    """把單一函式轉成 OpenAI tool schema dict。輸入再怪也不丟例外，盡量給合理預設值。"""
    try:
        name = fn.__name__
    except Exception:
        name = "unknown_function"

    try:
        doc = inspect.getdoc(fn)
    except Exception:
        doc = None

    try:
        sig = inspect.signature(fn)
    except (TypeError, ValueError):
        sig = None

    if sig is None:
        properties, required = {}, []
    else:
        properties, required = _properties(sig, param_descriptions(doc))

    return {
        "type": "function",
        "function": {
            "name": name,
            "description": summary(doc),
            "parameters": {
                "type": "object",
                "properties": properties,
                "required": required,
            },
        },
    }


def to_schemas(*fns) -> list:
    """把多個函式轉成 tool schema dict 的 list。"""
    return [to_schema(fn) for fn in fns]


def to_tools(*fns) -> tuple:
    """一次拿到 tool schema list 和 name -> function 的對照表。"""
    return to_schemas(*fns), {fn.__name__: fn for fn in fns}
