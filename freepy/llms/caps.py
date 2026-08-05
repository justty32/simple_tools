"""caps.py — 問 LiteLLM proxy：這個模型支不支援 tool calling / 看圖 / 思考？

答案有三種：True、False、None（proxy 沒說，就是不知道）。
查到的結果以 proxy 根位址為單位快取住，改完 litellm.yaml 重啟 proxy 後
記得呼叫 clear_cache()。查詢失敗一律吞掉當成「不知道」，絕不丟例外。
"""

import json
import urllib.request

# 我們關心的能力 -> /model/info 裡的欄位名
FIELDS = {
    "tools": "supports_function_calling",
    "vision": "supports_vision",
    "reasoning": "supports_reasoning",
}

# {root_url: {model_name: {"tools": bool|None, "vision": ..., "reasoning": ...}}}
_cache = {}


def clear_cache():
    """清空快取，強迫下次重新去問 proxy。"""
    _cache.clear()


def lookup(root_url, key, model, override=None):
    """回傳 {"tools", "vision", "reasoning"}。override 裡有的欄位優先，其餘看 proxy 怎麼說。"""
    remote = _table(root_url, key).get(model, {})
    override = override or {}
    return {
        name: override.get(name, remote.get(name)) for name in FIELDS
    }


def _table(root_url, key):
    """拿某個 proxy 的能力表；沒查過才去查（查不到的空表也算查過，不重試）。"""
    table = _cache.get(root_url)
    if table is None:
        table = _fetch(root_url, key)
        _cache[root_url] = table
    return table


def _fetch(root_url, key):
    """查 LiteLLM proxy 的 /model/info，失敗一律吞掉回傳空 dict。"""
    result = {}
    try:
        req = urllib.request.Request(
            root_url + "/model/info",
            headers={"Authorization": f"Bearer {key}"},
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        for entry in data.get("data", []):
            name = entry.get("model_name")
            info = entry.get("model_info", {})
            if name is None:
                continue
            result[name] = {k: info.get(field) for k, field in FIELDS.items()}
    except Exception:
        return {}
    return result
