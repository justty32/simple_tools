"""card.py — 一份 model card 的資料結構和讀檔。

格式規範在 FORMAT.md，可執行的那一半在 check.py。這裡只負責「拿到的東西怎麼用」：
剝掉出處拿值、alias 解析成 mode、能力表怎麼吐。

**`caps()` 預設只吐 `verified`。** `claimed` 是查來的情報不是結論 —— litellm 的
內建資料庫謊報過兩次，網搜來的東西沒有理由比它可信。
"""

import json

from .check import CAP_FIELDS, NATIVE_PARAMS, CardError, check


def load(path):
    """讀一份 card。讀不起來或形狀不對都丟 CardError。"""
    try:
        with open(path, encoding="utf-8") as handle:
            raw = json.load(handle)
    except (OSError, ValueError) as exc:
        raise CardError(f"{path}: 讀不起來：{exc}") from None
    return Card(raw, path)


class Card:
    """一顆模型的建議參數和能力。建構時就驗完，拿得到就是合法的。"""

    def __init__(self, raw, path=None):
        check(raw, path or (raw.get("id") if isinstance(raw, dict) else None) or "<card>")
        self.raw = raw
        self.path = path

    def __repr__(self):
        return f"<Card {self.id} ({self.runner})>"

    id = property(lambda self: self.raw["id"])
    weights = property(lambda self: self.raw["weights"])
    runner = property(lambda self: self.raw["runner"])
    aliases = property(lambda self: self.raw["aliases"])
    modes = property(lambda self: self.raw["modes"])
    sources = property(lambda self: self.raw["sources"])
    context = property(lambda self: (self.raw.get("context") or {}).get("v"))
    notes = property(lambda self: self.raw.get("notes", ""))
    claimed = property(lambda self: self.raw.get("claimed", {}))
    verified = property(lambda self: self.raw.get("verified", {}))

    def resolve(self, name):
        """name 是 alias 就照用，是 id 就取第一個 alias。回 (alias, mode)。"""
        if name in self.aliases:
            return name, self.aliases[name]
        alias = next(iter(self.aliases), None)
        if alias is None:
            raise CardError(f"{self.id} 一個 alias 都沒有，不知道要拿哪個名字去打")
        return alias, self.aliases[alias]

    def params(self, mode):
        """某個 mode 的建議參數，剝掉出處只留值。"""
        if mode not in self.modes:
            raise CardError(f"{self.id} 沒有 {mode!r} 這個 mode，有的是 {sorted(self.modes)}")
        return {name: node["v"] for name, node in self.modes[mode].items()}

    def needs_allowlist(self, mode):
        """這個 mode 裡有哪些參數會進 Params.extra —— 也就是可能被 drop_params 吞掉的。

        補進 litellm.yaml 的 allowed_openai_params 時照這份抄。
        """
        return sorted(n for n in self.modes[mode] if n not in NATIVE_PARAMS)

    def caps(self, trust="verified"):
        """能力表，只含真的有值的欄位（沒有的就讓 Engine 去問 proxy）。

        預設只吐 verified；要 claimed 得明講，而且 verified 一樣蓋在上面。
        """
        if trust not in ("verified", "claimed"):
            raise CardError(f"trust 只能是 'verified' 或 'claimed'，不是 {trust!r}")
        out = {}
        if trust == "claimed":
            out.update({k: node["v"] for k, node in self.claimed.items()})
        out.update({k: node["v"] for k, node in self.verified.items()})
        return out


__all__ = ["CAP_FIELDS", "NATIVE_PARAMS", "Card", "CardError", "load"]
