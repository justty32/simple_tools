"""Function creation checks; run on Linux."""

from pathlib import Path
import stat
import tempfile
import unittest

from ..cli import main
from ..new import FILES, RUN, create_function


class NewFunctionTests(unittest.TestCase):
    def test_create_minimal_function(self):
        with tempfile.TemporaryDirectory(prefix="agent-machine-", dir="/tmp") as temp:
            function = Path(temp) / "bot-a"

            result = main(["new", str(function)])

            self.assertEqual(result, 0)
            self.assertEqual(
                {item.name for item in function.iterdir()},
                {"run", *FILES},
            )
            self.assertEqual((function / "run").read_text(encoding="utf-8"), RUN)
            self.assertTrue((function / "run").stat().st_mode & stat.S_IXUSR)
            for name, content in FILES.items():
                self.assertEqual((function / name).read_text(encoding="utf-8"), content)

    def test_existing_path_is_not_modified(self):
        with tempfile.TemporaryDirectory(prefix="agent-machine-", dir="/tmp") as temp:
            function = Path(temp) / "bot-a"
            function.mkdir()
            marker = function / "mine"
            marker.write_bytes(b"keep")

            with self.assertRaises(FileExistsError):
                create_function(function)

            self.assertEqual(marker.read_bytes(), b"keep")


if __name__ == "__main__":
    unittest.main()
