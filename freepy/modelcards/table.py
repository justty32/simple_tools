"""table.py — 從 JSON 生一張人看的 markdown 表。

    cd freepy && uv run python -m modelcards table

**表不手寫第二份。**手寫的表一定會跟 JSON 不同步，而不同步的文件比沒有文件更糟。
"""


def render(cards):
    """一張總表，一顆模型一行。回字串，印不印是呼叫端的事。"""
    lines = ["| id | runner | context | modes | alias | 實打過 |",
             "|---|---|---|---|---|---|"]
    for card in cards:
        done = ", ".join(sorted(card.verified)) or "—"
        lines.append(
            f"| `{card.id}` | {card.runner} | {card.context or '—'} | "
            f"{', '.join(card.modes)} | {' '.join(f'`{a}`' for a in card.aliases)} | {done} |"
        )
    return "\n".join(lines)


def params_of(card):
    """單一模型的建議參數表，每個值後面掛出處編號。"""
    names = sorted({n for table in card.modes.values() for n in table})
    modes = list(card.modes)
    lines = [f"| 參數 | {' | '.join(modes)} |", "|---" * (len(modes) + 1) + "|"]
    for name in names:
        cells = []
        for mode in modes:
            node = card.modes[mode].get(name)
            cells.append("—" if node is None else f"{node['v']} [{node['src']}]")
        lines.append(f"| `{name}` | {' | '.join(cells)} |")
    return "\n".join(lines)
