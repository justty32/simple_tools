"""oneshot.py — 不需要記憶的一次性問答。"""

from .client import LLM


def ask(url, prompt, **kw):
    """一次性問答，不保留歷史：建立一個用完即丟的 LLM，呼叫 .ask(remember=False)。"""
    llm_kwargs = {"url": url}
    for k in ("model", "system", "key"):
        if k in kw and kw[k] is not None:
            llm_kwargs[k] = kw[k]

    llm = LLM(**llm_kwargs)
    return llm.ask(
        prompt=prompt,
        stream=kw.get("stream", False),
        images=kw.get("images"),
        tools=kw.get("tools"),
        params=kw.get("params"),
        remember=False,
    )
