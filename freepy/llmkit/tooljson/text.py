"""text.py — 執行結果怎麼收尾成給模型看的字串：解碼和裁切。

**只放每一種 `_type` 都成立的東西。** 判準跟 FORMAT.md 挑保留鍵那條一樣。
「輸出太長要截」和「二進位不要吐亂碼」對每種執行方式都成立 —— python 型的函式
一樣可能回一個十萬字的 list，塞進 context 就爆了，跟執行檔吐一坨 log 是同一件事。

原本這兩個函式在 `invoke.py` 裡，但那個檔是 exec 專屬的，第二種 `_type` 進來就
變成「要嘛跨進 exec 的地盤拿，要嘛複製一份常數」，兩個都不行，所以搬出來。
"""

#: 一次 tool 回傳最多塞給模型幾個字元。超過就截，截掉多少會寫在截斷處
MAX_OUTPUT = 30000


def decode(raw):
    """bytes → 給模型看的字串。含 NUL 就當二進位，不吐一堆替代字元灌爆 context。"""
    if not raw:
        return ""
    if b"\x00" in raw:
        return f"(binary output, {len(raw)} bytes, not shown)"
    return raw.decode("utf-8", errors="replace")


def clip(text, where="head", limit=MAX_OUTPUT):
    """太長就截，並註明省略了多少。編譯器那種重點在尾巴的用 tail。"""
    if len(text) <= limit:
        return text
    cut = len(text) - limit
    if where == "tail":
        return f"… [truncated, {cut} earlier characters]\n{text[-limit:]}"
    return f"{text[:limit]}\n… [truncated, {cut} more characters]"
