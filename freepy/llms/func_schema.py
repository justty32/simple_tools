"""func_schema.py — 把一般 python 函式轉成 OpenAI tool schema dict。

只做這一件事：靠 inspect.signature + type hints + docstring，組出
OpenAI function-calling 需要的 schema。不做驗證、不做執行、不做快取。

用法：
    schemas = to_schemas(get_weather)                 # 只要 schema
    schemas, dispatch = to_tools(get_weather)          # schema + name -> function 對照表
    # 收到 tool call 後：dispatch[call["name"]](**call["args"])
"""

import inspect
import json
import re
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


def _unwrap_optional(annotation):
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


def _json_type(annotation):
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


def _literal_values(annotation):
    """若 annotation 是 typing.Literal[...]，回傳裡面的值 list；否則回傳 None。"""
    if typing.get_origin(annotation) is typing.Literal:
        return list(typing.get_args(annotation))
    return None


def _summary(doc):
    """取 docstring 第一行非空白內容，當作函式的簡短描述。"""
    if not doc:
        return ""
    for line in doc.strip().splitlines():
        line = line.strip()
        if line:
            return line
    return ""


def _param_descriptions(doc):
    """從 docstring 解析每個參數的說明文字，依序嘗試三種常見格式。"""
    if not doc:
        return {}

    # 1. Google style 的 Args: 區塊，例如 "name: description"
    m = re.search(r"Args:\s*\n(.*?)(?:\n\s*\n|\Z)", doc, re.S)
    if m:
        found = {}
        for line in m.group(1).splitlines():
            pm = re.match(r"\s*(\w+)\s*(?:\([^)]*\))?\s*:\s*(.+)", line)
            if pm:
                found[pm.group(1)] = pm.group(2).strip()
        if found:
            return found

    # 2. Sphinx style 的 :param name: description
    sphinx = re.findall(r":param\s+(\w+)\s*:\s*(.+)", doc)
    if sphinx:
        return {name: desc.strip() for name, desc in sphinx}

    # 3. 最寬鬆的 fallback：文件字串裡任何一行 "name: description"
    simple = {}
    for line in doc.splitlines():
        pm = re.match(r"\s*(\w+)\s*:\s*(.+)", line)
        if pm:
            simple[pm.group(1)] = pm.group(2).strip()
    return simple


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

    description = _summary(doc)
    param_docs = _param_descriptions(doc)

    properties = {}
    required = []

    try:
        sig = inspect.signature(fn)
    except (TypeError, ValueError):
        sig = None

    if sig is not None:
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

            inner, optional = _unwrap_optional(annotation)

            prop = {"type": _json_type(inner)}
            literal_values = _literal_values(inner)
            if literal_values is not None:
                prop["enum"] = literal_values

            desc = param_docs.get(pname)
            if desc:
                prop["description"] = desc

            properties[pname] = prop

            has_default = param.default is not inspect.Parameter.empty
            if not has_default and not optional:
                required.append(pname)

    return {
        "type": "function",
        "function": {
            "name": name,
            "description": description,
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


if __name__ == "__main__":

    def get_weather(city: str, unit: typing.Literal["celsius", "fahrenheit"] = "celsius",
                     days: typing.Optional[int] = None):
        """查詢指定城市的天氣。

        Args:
            city: 城市名稱
            unit: 溫度單位
            days: 預報天數，不填則只回傳今天
        """

    print(json.dumps(to_schema(get_weather), ensure_ascii=False, indent=2))
