"""store.py — 讀整個 cards/，建一份「名字 → Card」的索引。

alias 和 id 都查得到同一張卡。撞名是用丟的：兩張卡搶同一個名字時靜靜選一張，
之後除錯會找不到北。
"""

import os

from .card import CardError, load

HERE = os.path.dirname(os.path.abspath(__file__))
CARDS = os.path.join(HERE, "cards")


def load_dir(directory=None):
    """讀一個資料夾裡的所有 .json，照檔名排序。任何一張壞掉就整批不給。"""
    directory = directory or CARDS
    try:
        names = sorted(os.listdir(directory))
    except OSError as exc:
        raise CardError(f"{directory}: 開不起來：{exc}") from None
    out = []
    for name in names:
        if not name.endswith(".json"):
            continue
        card = load(os.path.join(directory, name))
        if card.id != name[:-5]:
            raise CardError(f"{name}: 檔名和 id 對不上（id 是 {card.id!r}）")
        out.append(card)
    return out


def index(cards):
    """{alias 或 id: Card}。同一個名字被兩張卡宣告就丟 CardError。"""
    table = {}
    for card in cards:
        for name in (card.id, *card.aliases):
            other = table.get(name)
            if other is not None and other is not card:
                raise CardError(f"{name!r} 被 {other.id} 和 {card.id} 兩張卡搶")
            table[name] = card
    return table


def aliases(cards):
    """所有 litellm alias，排序後回傳。用來跟 proxy/litellm.yaml 對帳。"""
    return sorted(name for card in cards for name in card.aliases)
