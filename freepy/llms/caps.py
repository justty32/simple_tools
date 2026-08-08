"""caps.py — 問 LiteLLM proxy：這個端點加這顆模型，做得到哪些事？

答案有三種：True、False、None（proxy 沒說，就是不知道）。
查到的結果以 proxy 根位址為單位快取住，改完 litellm.yaml 重啟 proxy 後
記得呼叫 clear_cache()。查詢失敗一律吞掉當成「不知道」，絕不丟例外。

這裡列的欄位不全都會擋呼叫（擋不擋是 Engine.check() 決定的），
沒在擋的那幾個純粹是情報：值不值得去讀 reasoning、要不要為了快取排訊息順序。
"""

import json
import urllib.request

# 我們關心的能力 -> /model/info 裡的欄位名
FIELDS = {
    "tools": "supports_function_calling",
    "tool_choice": "supports_tool_choice",
    "parallel_tools": "supports_parallel_function_calling",
    "vision": "supports_vision",
    "reasoning": "supports_reasoning",
    "json_schema": "supports_response_schema",
    "caching": "supports_prompt_caching",
}

# {(root_url, key): {model_name: {"tools": bool|None, "vision": ..., ...}}}
# key 也要進 cache key：同一個 proxy 用不同金鑰問，看得到的模型可能不一樣
_cache = {}


def clear_cache():
    """清空快取，強迫下次重新去問 proxy。"""
    _cache.clear()


def lookup(root_url, key, model, override=None):
    """回傳一份完整的能力表（FIELDS 的每個 key 都在）。override 裡有的欄位優先。"""
    remote = _table(root_url, key).get(model, {})
    override = override or {}
    return {
        name: override.get(name, remote.get(name)) for name in FIELDS
    }


def _table(root_url, key):
    """拿某個 proxy 的能力表；沒查過才去查（查不到的空表也算查過，不重試）。"""
    table = _cache.get((root_url, key))
    if table is None:
        table = _fetch(root_url, key)
        _cache[(root_url, key)] = table
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
