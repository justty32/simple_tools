"""Portable offline checks for exec_tools discovery."""

import json
import os
from pathlib import Path
import stat
import tempfile

from . import DiscoveryError, roots, scan, spec_path, tools


def check(condition, label):
    if not condition:
        raise AssertionError(label)
    print(f"ok  {label}")


def executable(path: Path):
    path.write_text("fixture", encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def spec(name, program):
    return {
        "type": "function",
        "function": {
            "name": name,
            "description": f"fixture {name}",
            "parameters": {"type": "object", "properties": {}},
        },
        "_extra": {"_version": "0.1.0", "_type": "exec", "exec": [program]},
    }


def write(path: Path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data), encoding="utf-8")


def main():
    old = os.environ.get("FREEPY_TOOLS")
    try:
        with tempfile.TemporaryDirectory() as holder:
            home = Path(holder)
            first, second = home / "first", home / "second"
            first.mkdir()
            second.mkdir()
            executable(first / "covered.exe")
            executable(first / "covered_later.exe")
            executable(first / "missing.exe")
            executable(second / "later.exe")
            executable(first / ".hidden.exe")
            (first / "plain.txt").write_text("not executable", encoding="utf-8")
            write(first / ".specs" / "covered.json", spec("shared", "../covered.exe"))
            write(second / ".specs" / "later.json", [
                spec("shared", "../later.exe"), spec("second_only", "../later.exe"),
                spec("cross_root", "../../first/covered_later.exe"),
            ])
            (second / ".specs" / "broken.json").write_text("{", encoding="utf-8")

            value = os.pathsep.join([str(first), str(first), str(home / "absent"), str(second)])
            os.environ["FREEPY_TOOLS"] = value
            check(roots() == [first.resolve(), second.resolve()], "ordered roots and deduplication")
            check(spec_path(first / "covered.exe") ==
                  (first / ".specs" / "covered.exe.json").resolve(),
                  "conventional spec path")

            result = scan()
            check(list(result.specs) == ["shared", "second_only", "cross_root"],
                  "stable tool order")
            check(Path(result.specs["shared"].path).parent.parent == first.resolve(),
                  "first tool name wins")
            check(result.missing == [(first / "missing.exe").resolve()],
                  "cross-root coverage, missing, hidden, and plain files")
            check(len(result.errors) == 1 and result.errors[0].path.name == "broken.json",
                  "broken spec is isolated")

            try:
                tools()
            except DiscoveryError as exc:
                check("broken.json" in str(exc), "tools fails loudly on partial catalog")
            else:
                raise AssertionError("tools should reject a broken catalog")

            (second / ".specs" / "broken.json").unlink()
            schemas, dispatch = tools()
            expected = ["shared", "second_only", "cross_root"]
            check([item["function"]["name"] for item in schemas] == expected,
                  "tooljson schemas preserve precedence")
            check(list(dispatch) == expected, "tooljson dispatch is complete")
            check(scan([]).specs == {} and scan([]).missing == [], "explicit empty catalog")
    finally:
        if old is None:
            os.environ.pop("FREEPY_TOOLS", None)
        else:
            os.environ["FREEPY_TOOLS"] = old
    print("exec_tools: all checks passed")


if __name__ == "__main__":
    main()
