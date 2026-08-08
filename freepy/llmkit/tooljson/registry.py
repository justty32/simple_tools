"""registry.py — `_type` → 解析器。外面要加自己的執行方式就是往這裡登記。

    import tooljson

    class HttpBody:
        def __init__(self, spec): ...
        def run(self, args) -> str: ...
        target = None

    tooljson.register("http", HttpBody)

登記完，`_type: "http"` 的 .json 就跟內建的 `"exec"` 平起平坐 —— `load()` 讀得懂、
`tools()` 收得進 dispatch、`run()` 叫得動，**llmkit 一行都不用改**。

## body 要提供什麼

解析器是一個 callable，收一個 `Spec`，回一個 body 物件。那個物件只要兩樣東西：

| | |
|---|---|
| `run(args) -> str` | 收模型給的那包 dict，回一個字串。**錯誤也是字串，不丟例外** |
| `target` | 這份 spec 指向的本地檔案，`Spec.stale` 拿它算指紋；沒有就 `None` |

其餘都是那個 `_type` 自己的事。`ExecBody` 有 `argv` / `stdin` / `ok_exit` 那一堆，
是因為跑執行檔需要，不是因為 body 得長那樣。

建構時發現 .json 寫錯就丟 `SpecError`（用 `spec.need()`）—— 那是設定錯，越早炸越好。
`run()` 裡發生的事則一律回字串，因為那個字串會直接變成送回模型的 tool message。

## 撞名

**同一個 `_type` 登記兩次就覆蓋**，不擋。理由是這種事只有一種情境：你在 REPL 裡
反覆改自己的解析器。真的撞到別人的名字是命名問題，不是註冊表該擋的。
"""

_PARSERS = {}


def register(kind, parser):
    """登記一個 `_type`。kind 是 `_extra._type` 裡那個字串，parser 收 Spec 回 body。"""
    if not isinstance(kind, str) or not kind:
        raise ValueError(f"_type 要是非空字串，拿到 {kind!r}")
    if not callable(parser):
        raise ValueError(f"{kind!r} 的解析器要是 callable，拿到 {parser!r}")
    _PARSERS[kind] = parser
    return parser


def parser(kind):
    """查一個 `_type` 的解析器，沒登記過回 None（由呼叫端決定怎麼報錯）。"""
    return _PARSERS.get(kind)


def types():
    """目前登記了哪些 `_type`，排序後回傳。錯誤訊息會用它列出「認得哪些」。"""
    return sorted(_PARSERS)
