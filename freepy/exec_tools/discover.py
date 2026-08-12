"""Discover explicitly configured tooljson specs.

`tooljson` deliberately has no search path.  This module adds the next, small
layer: scan directories selected by the caller or by ``FREEPY_TOOLS``.  It
never scans ``PATH`` and never turns an executable into a tool by itself.
"""

from dataclasses import dataclass, field
import os
from pathlib import Path

from tooljson import Spec, SpecError, load_all

ENV = "FREEPY_TOOLS"
SPEC_DIR = ".specs"


@dataclass(frozen=True)
class Problem:
    """One spec file that could not be loaded."""

    path: Path
    message: str


@dataclass
class ScanResult:
    """Complete discovery result; one bad spec does not hide the others."""

    specs: dict[str, Spec] = field(default_factory=dict)
    missing: list[Path] = field(default_factory=list)
    errors: list[Problem] = field(default_factory=list)


def roots(value=None) -> list[Path]:
    """Resolve existing directories, preserving order and removing repeats.

    ``None`` reads ``FREEPY_TOOLS``.  A string uses ``os.pathsep`` like PATH;
    an iterable accepts path-like objects and is useful for embedded callers.
    Missing directories are ignored and can therefore be mounted later.
    """
    raw = os.environ.get(ENV, "") if value is None else value
    if isinstance(raw, str):
        pieces = raw.split(os.pathsep)
    elif isinstance(raw, os.PathLike):
        pieces = [raw]
    else:
        pieces = raw
    found, seen = [], set()
    for piece in pieces:
        if not str(piece).strip():
            continue
        path = Path(piece).expanduser().resolve()
        key = os.path.normcase(str(path))
        if key not in seen and path.is_dir():
            found.append(path)
            seen.add(key)
    return found


def _children(directory: Path) -> list[Path]:
    try:
        return sorted(directory.iterdir(), key=lambda item: item.name)
    except OSError:
        return []


def _executable(path: Path) -> bool:
    if os.name == "nt":
        extensions = {item.lower() for item in os.environ.get(
            "PATHEXT", ".COM;.EXE;.BAT;.CMD"
        ).split(os.pathsep)}
        return path.suffix.lower() in extensions
    return os.access(path, os.X_OK)


def executables(directory) -> list[Path]:
    """List visible executable files directly inside one directory."""
    return [
        path.resolve() for path in _children(Path(directory))
        if not path.name.startswith(".") and path.is_file() and _executable(path)
    ]


def spec_files(directory) -> list[Path]:
    """List lower-case ``.json`` files directly inside ``.specs``."""
    return [path.resolve() for path in _children(Path(directory) / SPEC_DIR)
            if path.is_file() and path.suffix == ".json"]


def spec_path(executable) -> Path:
    """Return the conventional cache path for one executable's spec."""
    path = Path(executable).expanduser().resolve()
    return path.parent / SPEC_DIR / f"{path.name}.json"


def _path_key(path) -> str | None:
    return os.path.normcase(str(Path(path).resolve())) if path else None


def scan(directories=None) -> ScanResult:
    """Find specs, uncovered executables, and broken spec files.

    Tool names obey PATH-like precedence: the first configured directory wins.
    A spec can contain several tools.  Broken files are reported without
    suppressing valid tools from the same or later directories.
    """
    result, covered = ScanResult(), set()
    directories = roots(directories)
    for root in directories:
        for path in spec_files(root):
            try:
                loaded = load_all(path)
            except SpecError as exc:
                result.errors.append(Problem(path, str(exc)))
                continue
            for spec in loaded:
                result.specs.setdefault(spec.name, spec)
                target = _path_key(spec.body.target)
                if target:
                    covered.add(target)
    result.missing = [
        path for root in directories for path in executables(root)
        if _path_key(path) not in covered
    ]
    return result
