"""args.py — 模型給的一包 JSON → 一條 argv 和一段 stdin。純函式，不碰 process。

分出來是因為「怎麼展開」是格式契約的一部分（別的語言的 lib 要展開成一樣的東西），
而「怎麼跑」是這個實作自己的事。規則見 FORMAT.md，這裡只是把它寫成程式。

**沒有 kind 這種欄位**，命令列上長什麼樣是推導出來的：沒 flag 就是位置參數；
有 flag 又是 boolean 就是開關（真值才放旗標）；其餘是旗標加值。schema 的型別是
唯一真相，同一件事不講第二遍。

檢查失敗一律回**字串**，因為那個字串會直接變成送回模型的 tool message ——
它讀到「text 太長了，65KB 上限」是能自己重試的，讀到例外只會整條斷掉。
"""

import json

#: linux 的 MAX_ARG_STRLEN，單一 argv 項目超過就 E2BIG。這是物理限制，不用宣告就擋
ARG_MAX_BYTES = 128 * 1024
#: 「送了但型別轉不過去」的哨兵，跟模型明確送 null（合法的「這個我不給」）分開
BAD = object()


def _render(value):
    """值 → 命令列上的字串。照 JSON 的字面寫法，這樣別的語言的 lib 才對得起來。"""
    if isinstance(value, str):
        return value
    if isinstance(value, bool):  # 要擋在 int 前面，bool 是 int 的子類
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return json.dumps(value)
    return json.dumps(value, ensure_ascii=False)


def _coerce(value, want):
    """小模型很常把 800 送成 "800"。schema 說要什麼就試著轉，轉不動回 BAD。"""
    if not isinstance(value, str):
        return value
    try:
        if want == "integer":
            return int(value.strip())
        if want == "number":
            return float(value.strip())
    except ValueError:
        return BAD
    if want == "boolean":
        low = value.strip().lower()
        return low == "true" if low in ("true", "false") else BAD
    return value


def _check(spec, args):
    """required 有沒有漏、有沒有多給不認得的、值有沒有超標。過了回 None。"""
    missing = [p for p in spec.required if args.get(p) is None]
    if missing:
        return f"Error: missing required argument(s): {', '.join(missing)}"
    unknown = sorted(k for k in args if k not in spec.props)
    if unknown:
        # 安靜丟掉模型明明有給的東西，跟 litellm 的 drop_params 是同一個坑
        return (f"Error: unknown argument(s): {', '.join(unknown)}. "
                f"This tool accepts: {', '.join(sorted(spec.props)) or '(none)'}")
    for name, rule in (spec.body.limits or {}).items():
        bad = _limit(name, args.get(name), rule)
        if bad:
            return bad
    return None


def _limit(name, value, rule):
    if value is None or not isinstance(rule, dict):
        return None
    cap = rule.get("max_bytes")
    if cap is not None and isinstance(value, str):
        size = len(value.encode("utf-8"))
        if size > cap:
            return f"Error: argument {name!r} is {size} bytes, over the {cap} limit"
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if rule.get("min") is not None and value < rule["min"]:
            return f"Error: argument {name!r} is {value}, below the minimum {rule['min']}"
        if rule.get("max") is not None and value > rule["max"]:
            return f"Error: argument {name!r} is {value}, above the maximum {rule['max']}"
    return None


def _one(name, value, bind, boolean, out):
    """展開一個值。回錯誤字串或 None。"""
    flag = bind.get("flag")
    if boolean and flag:
        if value:
            out.append(flag)  # 開關本身不帶值，假值連旗標都不放
        return None
    text = _render(value)
    if len(text.encode("utf-8")) > ARG_MAX_BYTES:
        return (f"Error: argument {name!r} is too long for the command line "
                f"({len(text.encode('utf-8'))} bytes, limit {ARG_MAX_BYTES})")
    if not flag:
        out.append(text)
    elif bind.get("separate", True):
        out.extend([flag, text])
    else:
        out.append(f"{flag}={text}")
    return None


def build(spec, args):
    """回 (argv, stdin, err)。err 是字串就別跑了，直接把它交回給模型。"""
    if not isinstance(args, dict):
        return None, None, f"Error: arguments must be a JSON object, got {type(args).__name__}"
    types = {k: (v or {}).get("type") for k, v in spec.props.items()}
    args = {k: _coerce(v, types.get(k)) for k, v in args.items()}
    bad = sorted(k for k, v in args.items() if v is BAD)
    if bad:
        return None, None, f"Error: argument(s) {', '.join(bad)} have the wrong type"
    args = {k: v for k, v in args.items() if v is not None}  # 明確的 null 等於沒給
    err = _check(spec, args)
    if err:
        return None, None, err

    body = spec.body
    argv = list(body.exec)
    for name, bind in body.order:
        if name == body.stdin_param or name not in args:
            continue  # 缺席的參數整條跳過，不產生空字串也不產生 --no-xxx
        value = args[name]
        repeat = bool(bind.get("repeat"))
        if isinstance(value, list) and not repeat:
            return None, None, f"Error: argument {name!r} does not take a list of values"
        for item in (value if repeat and isinstance(value, list) else [value]):
            err = _one(name, item, bind, types.get(name) == "boolean", argv)
            if err:
                return None, None, err

    stdin = None
    if body.stdin_param is not None and args.get(body.stdin_param) is not None:
        stdin = _render(args[body.stdin_param])
    return argv, stdin, None
