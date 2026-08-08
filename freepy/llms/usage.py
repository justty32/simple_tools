"""usage.py — 把 SDK 的 usage 物件壓成純 dict。

這一輪花了多少 token。cached 是命中前綴快取的部分（前綴一字不差才會有），
reasoning 是思考燒掉的 —— 兩個都常常缺，缺就是 None，不是 0。

串流要拿到 usage 得先送 stream_options={"include_usage": True}（engine.py 有開），
不然最後那片不會帶，永遠是 None。
"""


def usage_dict(usage):
    """SDK 的 usage 物件 -> 純 dict；沒有就回 None。"""
    if usage is None:
        return None
    prompt_details = getattr(usage, "prompt_tokens_details", None)
    done_details = getattr(usage, "completion_tokens_details", None)
    return {
        "prompt": getattr(usage, "prompt_tokens", None),
        "completion": getattr(usage, "completion_tokens", None),
        "total": getattr(usage, "total_tokens", None),
        "cached": getattr(prompt_details, "cached_tokens", None),
        "reasoning": getattr(done_details, "reasoning_tokens", None),
    }
