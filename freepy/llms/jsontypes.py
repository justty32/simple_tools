"""jsontypes.py — python type annotation → JSON schema type。

只認得 OpenAI function schema 需要的那幾種，不認得的一律當 string，
寧可給個能用的預設值，也不要丟例外。
"""

import types
import typing

# python type -> JSON schema type 的對照表
_TYPE_MAP = {
    str: "string",
    int: "integer",
    float: "number",
    bool: "boolean",
    list: "array",
    dict: "object",
}


def unwrap_optional(annotation):
    """把 Optional[X] / X | None 攤平成 (X, True)；不是 Optional 就原樣回傳 (annotation, False)。"""
    origin = typing.get_origin(annotation)
    is_union = origin is typing.Union or (
        hasattr(types, "UnionType") and origin is types.UnionType
    )
    if is_union:
        args = typing.get_args(annotation)
        non_none = [a for a in args if a is not type(None)]
        if type(None) in args and len(non_none) == 1:
            return non_none[0], True
    return annotation, False


def json_type(annotation):
    """把 type annotation 轉成 JSON schema type 字串，未知型別一律當 string。"""
    origin = typing.get_origin(annotation)

    if origin is typing.Literal:
        return "string"

    if origin in (list, typing.List):
        return "array"
    if origin in (dict, typing.Dict):
        return "object"

    if annotation in _TYPE_MAP:
        return _TYPE_MAP[annotation]

    return "string"


def literal_values(annotation):
    """若 annotation 是 typing.Literal[...]，回傳裡面的值 list；否則回傳 None。"""
    if typing.get_origin(annotation) is typing.Literal:
        return list(typing.get_args(annotation))
    return None
