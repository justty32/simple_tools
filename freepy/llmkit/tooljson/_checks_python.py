"""Portable offline checks for deterministic explicit Python tool sources."""

import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from . import Spec, SpecError
from .spec import fingerprint


PREFIX = "_tooljson_source_probe"


def check(condition, label):
    if not condition:
        raise AssertionError(label)
    print(f"ok  {label}")


def data(module, path, source=None):
    extra = {"_version": "0.1.0", "_type": "python",
             "module": module, "attr": "identify", "path": str(path)}
    if source:
        extra["source"] = source
    return {"type": "function", "function": {"name": "identify"}, "_extra": extra}


def rejected(module, path):
    try:
        Spec(data(module, path))
    except SpecError as exc:
        return "已從別處載入" in str(exc)
    return False


def write_module(path, answer):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"def identify():\n    return {answer!r}\n", encoding="utf-8")


def forget():
    for name in list(sys.modules):
        if name == PREFIX or name.startswith(PREFIX + ".") or name.startswith(PREFIX + "_"):
            sys.modules.pop(name, None)


def main():
    old_path = list(sys.path)
    try:
        with TemporaryDirectory() as temp:
            root = Path(temp)
            left, right = root / "left", root / "right"
            direct = f"{PREFIX}_direct"
            write_module(left / f"{direct}.py", "left")
            write_module(right / f"{direct}.py", "right")

            first = Spec(data(direct, left, fingerprint(left / f"{direct}.py")))
            check(first.run({}) == "left" and first.stale is False,
                  "directory path selects the requested source and tracks staleness")
            check(Spec(data(direct, left)).run({}) == "left",
                  "reloading the same explicit source reuses its module")
            check(rejected(direct, right),
                  "a cached same-name module cannot override another directory path")

            sys.modules.pop(direct, None)
            file_name = f"{PREFIX}_file"
            left_file, right_file = left / "one.py", right / "two.py"
            write_module(left_file, "left file")
            write_module(right_file, "right file")
            check(Spec(data(file_name, left_file)).run({}) == "left file",
                  "file path loads its exact source")
            check(rejected(file_name, right_file),
                  "a cached same-name module cannot override another file path")

            sys.modules.pop(file_name, None)
            package = PREFIX
            for side, answer in ((left, "left package"), (right, "right package")):
                (side / package).mkdir(parents=True)
                (side / package / "__init__.py").write_text("", encoding="utf-8")
                write_module(side / package / "anchor.py", answer)
                write_module(side / package / "tool.py", answer)
            check(Spec(data(f"{package}.anchor", left)).run({}) == "left package",
                  "dotted module loads through the requested package")
            check(rejected(f"{package}.tool", right),
                  "a cached parent package cannot redirect a new child module")
    finally:
        forget()
        sys.path[:] = old_path
    print("tooljson python-source checks: all passed")


if __name__ == "__main__":
    main()
