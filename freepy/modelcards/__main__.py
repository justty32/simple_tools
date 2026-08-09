"""__main__.py — 入口。

    cd freepy && uv run python -m modelcards          # 離線煙霧測試（關卡在 smoke.py）
    cd freepy && uv run python -m modelcards table    # 人看的表（產生器在 table.py）

`llms` 在 `llmkit/` 那層，不在這層，所以入口自己補一次搜尋路徑 —— 比照 try.py。
lib 本身不動 sys.path：package 偷改全域狀態比 import 錯誤更難查。
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "llmkit"))

from . import cards, reload, table  # noqa: E402
from . import smoke  # noqa: E402


def main():
    reload()
    if len(sys.argv) > 1 and sys.argv[1] == "table":
        print(table.render(cards()))
        for card in cards():
            print(f"\n### `{card.id}`\n\n{table.params_of(card)}")
        return 0
    return 0 if smoke.run() else 1


sys.exit(main())
