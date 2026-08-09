"""modelcards — 每顆模型的官方建議參數和能力，記成 JSON，用的時候一行套上去。

    from llms import LLM
    from modelcards import engine_for

    bot = LLM(engine_for("lm-qwen3.5-9b"), system="你是個惜字如金的助手")

方向是單向的：這裡 import `llms`，`llms` 不知道這包存在 —— 抽掉 modelcards
llms 照樣能用。`llms` 是延遲 import 的，只讀資料不需要裝 openai。

**`caps_for()` 預設只吐實打過的（`verified`）。** 查來的 `claimed` 是情報不是結論，
要它得明講 `trust="claimed"`。理由見 README.md：litellm 的內建資料庫謊報過兩次，
網搜來的東西沒有理由比它可信。
"""

from .card import CAP_FIELDS, NATIVE_PARAMS, Card, CardError, load
from .store import CARDS, aliases, index, load_dir

__all__ = [
    "CAP_FIELDS", "CARDS", "NATIVE_PARAMS", "Card", "CardError",
    "aliases", "cards", "caps_for", "engine_for", "find", "index",
    "load", "load_dir", "params_for", "reload",
]

_table = None


def _llms():
    """延遲拿 llms。它在 llmkit/ 那層，不在這層 —— import 不到就講清楚怎麼修。"""
    try:
        import llms
    except ImportError:
        raise CardError(
            "import 不到 llms。它在 freepy/llmkit/ 底下，那層要在 sys.path 裡 —— "
            "從 freepy/llmkit 跑，或比照 try.py 自己 sys.path.insert 一次。"
        ) from None
    return llms


def reload(directory=None):
    """重讀 cards/。改完 JSON 不用重開 python。"""
    global _table
    _table = index(load_dir(directory))
    return _table


def _lookup():
    return _table if _table is not None else reload()


def cards():
    """所有 card，照 id 排序。"""
    return sorted(set(_lookup().values()), key=lambda card: card.id)


def find(name):
    """用 alias 或 id 拿一張卡。找不到就丟，不回 None —— 打錯字要當場知道。"""
    table = _lookup()
    card = table.get(name)
    if card is None:
        raise CardError(f"沒有 {name!r} 這張卡。有的是：{sorted(table)}")
    return card


def params_for(name, **override):
    """建議參數 → llms.Params。Params 沒有欄位的參數自動包進 `extra_body`。

    override 蓋掉卡上的值，也可以加卡上沒有的（一樣照欄位決定去處）。

    **為什麼是 `extra_body` 而不是攤平在 extra 裡**：`top_k` / `min_p` 這種非 OpenAI
    的參數當成頂層 kwarg 送，openai SDK 在送出前就 `TypeError` 擋下來
    （2026-08-09 對 LM Studio 實打）。包進 `extra_body` 才會原封不動進 JSON body。
    """
    card = find(name)
    _, mode = card.resolve(name)
    values = card.params(mode)
    values.update(override)
    native = {k: v for k, v in values.items() if k in NATIVE_PARAMS}
    body = {k: v for k, v in values.items() if k not in NATIVE_PARAMS}
    return _llms().Params(**native, extra={"extra_body": body} if body else {})


def caps_for(name, trust="verified"):
    """能力表，只含有值的欄位；其餘留白讓 Engine 去問 proxy。"""
    return find(name).caps(trust)


def engine_for(name, trust="verified", **kwargs):
    """組好參數和能力的 llms.Engine。model 用的是 litellm alias，不是 card id。

    kwargs 直接透傳給 Engine（url / key / timeout…），也蓋得掉 params / caps。
    """
    card = find(name)
    alias, _ = card.resolve(name)
    kwargs.setdefault("params", params_for(name))
    kwargs.setdefault("caps", caps_for(name, trust))
    return _llms().Engine(model=alias, **kwargs)
