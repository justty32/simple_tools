from __future__ import annotations

import unittest
from pathlib import Path


class ProcessSizeBudget(unittest.TestCase):
    def test_new_process_modules_stay_within_8k(self) -> None:
        here = Path(__file__).resolve().parent
        sizes = {path.name: path.stat().st_size for path in [*here.glob("p1a2_process*.py"), *here.glob("test_process*.py")]}
        self.assertTrue(sizes)
        self.assertTrue(all(size <= 8192 for size in sizes.values()), sizes)


if __name__ == "__main__": unittest.main()
