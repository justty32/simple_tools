"""Strict, single-writer filesystem primitives for the P1a-2 workbench."""
from __future__ import annotations

import contextlib
import fcntl
import hashlib
import json
import os
import re
import stat
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator

MAX_JSON = 1024 * 1024
MAX_DIRECTORY_ENTRIES = 64
MAX_TREE_NODES = 4096
MAX_TREE_BYTES = 64 * MAX_JSON
MAX_TREE_DEPTH = 64
HEX64_RE = re.compile(r"[0-9a-f]{64}")
ROOT_ID_RE = re.compile(r"[a-z][a-z0-9_-]{0,31}")
TASK_ID_RE = re.compile(r"[a-z][a-z0-9_-]{0,63}")
TEMP_TOKEN_RE = re.compile(r"[A-Za-z0-9_-]{6,32}")


class StoreError(RuntimeError):
    """Base class for expected workbench failures."""


class Corruption(StoreError):
    """Committed or filesystem state contradicts the prototype grammar."""


class AcceptanceRejected(StoreError):
    """A new acceptance request is invalid or already planned."""


def _reject_floats(value: Any, label: str) -> None:
    if isinstance(value, float):
        raise Corruption(label + ": floats are forbidden")
    if isinstance(value, list):
        for item in value:
            _reject_floats(item, label)
    elif isinstance(value, dict):
        for key, item in value.items():
            if not isinstance(key, str):
                raise ValueError(label + ": object key must be string")
            _reject_floats(item, label)


def canonical_json(value: Any) -> bytes:
    _reject_floats(value, "canonical JSON")
    return (json.dumps(value, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def ensure_canonical_size(value: Any, limit: int, label: str) -> None:
    """Count exact compact-JSON byte size without first building the JSON bytes."""
    total = 1  # final newline

    def add(amount: int) -> None:
        nonlocal total
        total += amount
        if total > limit:
            raise Corruption(label + ": canonical JSON exceeds bounded limit")

    def string_size(value: str) -> int:
        size = 2  # quotes
        for char in value:
            code = ord(char)
            if char in ('"', "\\") or code in (0x08, 0x09, 0x0A, 0x0C, 0x0D):
                size += 2
            elif code < 0x20:
                size += 6
            elif code < 0x80:
                size += 1
            elif code < 0x800:
                size += 2
            elif 0xD800 <= code <= 0xDFFF:
                raise Corruption(label + ": invalid Unicode surrogate")
            elif code < 0x10000:
                size += 3
            else:
                size += 4
        return size

    def visit(item: Any) -> None:
        if item is None:
            add(4)
        elif item is True:
            add(4)
        elif item is False:
            add(5)
        elif isinstance(item, int):
            add(len(str(item)))
        elif isinstance(item, float):
            raise Corruption(label + ": floats are forbidden")
        elif isinstance(item, str):
            add(string_size(item))
        elif isinstance(item, list):
            add(2 + max(0, len(item) - 1))
            for child in item:
                visit(child)
        elif isinstance(item, dict):
            add(2 + max(0, len(item) - 1))
            for key, child in item.items():
                if not isinstance(key, str):
                    raise Corruption(label + ": object key must be string")
                add(string_size(key) + 1)
                visit(child)
        else:
            raise Corruption(label + ": unsupported JSON value")

    visit(value)


def strict_json(data: bytes, label: str) -> Any:
    if len(data) > MAX_JSON:
        raise Corruption(label + ": JSON exceeds 1 MiB")

    def no_dupes(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate key: " + key)
            result[key] = value
        return result

    def bad_float(value: str) -> None:
        raise ValueError("float/non-finite number: " + value)

    try:
        text = data.decode("utf-8")
        decoder = json.JSONDecoder(object_pairs_hook=no_dupes,
                                   parse_float=bad_float,
                                   parse_constant=bad_float)
        value, end = decoder.raw_decode(text)
        if text[end:].strip():
            raise ValueError("trailing data")
        _reject_floats(value, label)
        # This also rejects decoded unpaired surrogates.
        canonical_json(value)
        return value
    except (UnicodeDecodeError, UnicodeEncodeError, ValueError, json.JSONDecodeError) as exc:
        raise Corruption(label + ": invalid strict JSON") from exc


def exact_fields(value: Any, fields: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != fields:
        actual = set(value) if isinstance(value, dict) else set()
        raise Corruption(f"{label}: unexpected/missing fields {sorted(actual ^ fields)}")
    return value


def integer(value: Any, label: str, minimum: int = 0, maximum: int | None = None) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise Corruption(label + ": integer required")
    if value < minimum or (maximum is not None and value > maximum):
        raise Corruption(label + ": integer out of range")
    return value


def text_value(value: Any, label: str) -> str:
    if not isinstance(value, str):
        raise Corruption(label + ": string required")
    return value


def root_id(value: Any, label: str = "root_id") -> str:
    value = text_value(value, label)
    if ROOT_ID_RE.fullmatch(value) is None or "--" in value:
        raise Corruption(label + ": invalid root ID")
    return value


def task_id(value: Any, label: str = "task_id") -> str:
    value = text_value(value, label)
    if TASK_ID_RE.fullmatch(value) is None:
        raise Corruption(label + ": invalid Task ID")
    return value


def hex64(value: Any, label: str) -> str:
    value = text_value(value, label)
    if HEX64_RE.fullmatch(value) is None:
        raise Corruption(label + ": lowercase SHA-256 required")
    return value


def make_ref(data: bytes) -> dict[str, Any]:
    return {"sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


def validate_ref(value: Any, label: str = "ref") -> dict[str, Any]:
    value = exact_fields(value, {"sha256", "size"}, label)
    return {"sha256": hex64(value["sha256"], label + ".sha256"),
            "size": integer(value["size"], label + ".size")}


def fsync_directory(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _validate_existing_ancestors(path: Path) -> None:
    chain = list(path.parents)[::-1] + [path]
    for item in chain:
        try:
            info = os.lstat(item)
        except FileNotFoundError:
            continue
        except OSError as exc:
            raise Corruption("cannot lstat path ancestor: " + str(item)) from exc
        if stat.S_ISLNK(info.st_mode):
            raise Corruption("symlink rejected in path ancestor: " + str(item))


def directory_chain_snapshot(path: Path, label: str) -> tuple[tuple[Any, ...], ...]:
    """Bind a lexical absolute path to its current non-symlink directory chain.

    Shared ancestors such as /tmp may legitimately change mtime while a resolver
    runs, so the guard records identity, type, ownership, and permissions rather
    than mutable directory-content metadata.
    """
    rows: list[tuple[Any, ...]] = []
    for item in list(path.parents)[::-1] + [path]:
        try:
            info = os.lstat(item)
        except OSError as exc:
            raise Corruption(label + ": path ancestor is missing or unreadable") from exc
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
            raise Corruption(label + ": path ancestor must be a non-symlink directory")
        rows.append((os.fspath(item), info.st_dev, info.st_ino, info.st_mode,
                     info.st_uid, info.st_gid))
    return tuple(rows)


def require_directory(path: Path, label: str, *, required: bool = True) -> bool:
    try:
        info = os.lstat(path)
    except FileNotFoundError:
        if required:
            raise Corruption(label + ": missing directory")
        return False
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
        raise Corruption(label + ": immediate non-symlink directory required")
    return True


def list_directory(path: Path, label: str) -> list[os.DirEntry[str]]:
    require_directory(path, label)
    entries: list[os.DirEntry[str]] = []
    with os.scandir(path) as scan:
        for entry in scan:
            if len(entries) == MAX_DIRECTORY_ENTRIES:
                raise Corruption(label + ": more than 64 entries")
            entries.append(entry)
    return entries


def read_regular(path: Path, limit: int, label: str, *, required: bool = True) -> bytes | None:
    try:
        before = os.lstat(path)
    except FileNotFoundError:
        if required:
            raise Corruption(label + ": missing regular file")
        return None
    except OSError as exc:
        raise Corruption(label + ": cannot lstat regular file") from exc
    if stat.S_ISLNK(before.st_mode) or not stat.S_ISREG(before.st_mode):
        raise Corruption(label + ": regular non-symlink file required")
    if before.st_size > limit:
        raise Corruption(label + ": file exceeds bounded limit")
    flags = os.O_RDONLY | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags)
    except FileNotFoundError as exc:
        raise Corruption(label + ": disappeared during open") from exc
    except OSError as exc:
        raise Corruption(label + ": cannot open regular file") from exc
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise Corruption(label + ": regular non-symlink file required")
        if info.st_size > limit:
            raise Corruption(label + ": file exceeds bounded limit")
        chunks: list[bytes] = []
        remaining = info.st_size
        while remaining:
            chunk = os.read(fd, min(remaining, 1024 * 1024))
            if not chunk:
                raise Corruption(label + ": short read")
            chunks.append(chunk)
            remaining -= len(chunk)
        extra = os.read(fd, 1)
        if extra:
            raise Corruption(label + ": grew during bounded read")
        return b"".join(chunks)
    finally:
        os.close(fd)


def bounded_tree_snapshot(path: Path, label: str) -> tuple[tuple[Any, ...], ...]:
    """Fingerprint a finite tree without following or opening special files.

    This is an acceptance-resolver guard, not a persisted format.  Regular files
    are hashed through bounded streaming reads; symlinks and special files are
    represented only by their own lstat metadata (plus the symlink text).
    """
    rows: list[tuple[Any, ...]] = []
    byte_count = 0

    def metadata(info: os.stat_result) -> tuple[int, ...]:
        return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink,
                info.st_uid, info.st_gid, info.st_size, info.st_mtime_ns,
                info.st_ctime_ns, info.st_rdev)

    def reserve_node() -> None:
        if len(rows) >= MAX_TREE_NODES:
            raise Corruption(label + ": tree exceeds bounded node limit")

    def same_identity(left: os.stat_result, right: os.stat_result) -> bool:
        return metadata(left) == metadata(right)

    def regular_digest(directory_fd: int, name: str,
                       before: os.stat_result) -> str:
        nonlocal byte_count
        if before.st_size > MAX_TREE_BYTES - byte_count:
            raise Corruption(label + ": tree exceeds bounded byte limit")
        flags = os.O_RDONLY | os.O_CLOEXEC
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        if hasattr(os, "O_NONBLOCK"):
            flags |= os.O_NONBLOCK
        fd = os.open(name, flags, dir_fd=directory_fd)
        try:
            opened = os.fstat(fd)
            if not stat.S_ISREG(opened.st_mode) or not same_identity(before, opened):
                raise Corruption(label + ": regular file changed during snapshot")
            digest = hashlib.sha256()
            remaining = opened.st_size
            while remaining:
                chunk = os.read(fd, min(remaining, 1024 * 1024))
                if not chunk:
                    raise Corruption(label + ": regular file shortened during snapshot")
                digest.update(chunk)
                remaining -= len(chunk)
            if os.read(fd, 1):
                raise Corruption(label + ": regular file grew during snapshot")
            after = os.fstat(fd)
            if not same_identity(opened, after):
                raise Corruption(label + ": regular file changed during snapshot")
            byte_count += opened.st_size
            return digest.hexdigest()
        finally:
            os.close(fd)

    def visit_directory(directory_fd: int, relative: str, depth: int) -> None:
        if depth > MAX_TREE_DEPTH:
            raise Corruption(label + ": tree exceeds bounded depth limit")
        before_directory = os.fstat(directory_fd)
        if not stat.S_ISDIR(before_directory.st_mode):
            raise Corruption(label + ": directory changed during snapshot")
        reserve_node()
        rows.append((relative, "directory", metadata(before_directory), None))

        names: list[str] = []
        with os.scandir(directory_fd) as scan:
            for entry in scan:
                if len(names) == MAX_DIRECTORY_ENTRIES:
                    raise Corruption(label + ": directory has more than 64 entries")
                names.append(entry.name)
        names.sort(key=os.fsencode)

        for name in names:
            child_relative = name if relative == "." else relative + "/" + name
            before = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
            if stat.S_ISDIR(before.st_mode):
                flags = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC
                if hasattr(os, "O_NOFOLLOW"):
                    flags |= os.O_NOFOLLOW
                child_fd = os.open(name, flags, dir_fd=directory_fd)
                try:
                    opened = os.fstat(child_fd)
                    if not stat.S_ISDIR(opened.st_mode) or not same_identity(before, opened):
                        raise Corruption(label + ": directory changed during snapshot")
                    visit_directory(child_fd, child_relative, depth + 1)
                finally:
                    os.close(child_fd)
                after = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
                if not same_identity(before, after):
                    raise Corruption(label + ": directory changed during snapshot")
            elif stat.S_ISREG(before.st_mode):
                reserve_node()
                digest = regular_digest(directory_fd, name, before)
                after = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
                if not same_identity(before, after):
                    raise Corruption(label + ": regular file changed during snapshot")
                rows.append((child_relative, "regular", metadata(before), digest))
            elif stat.S_ISLNK(before.st_mode):
                reserve_node()
                target = os.readlink(name, dir_fd=directory_fd)
                if len(os.fsencode(target)) > 4096:
                    raise Corruption(label + ": symlink text exceeds bounded limit")
                after = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
                if not same_identity(before, after):
                    raise Corruption(label + ": symlink changed during snapshot")
                rows.append((child_relative, "symlink", metadata(before), target))
            else:
                reserve_node()
                after = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
                if not same_identity(before, after):
                    raise Corruption(label + ": special file changed during snapshot")
                rows.append((child_relative, "special", metadata(before), None))

        after_directory = os.fstat(directory_fd)
        if not same_identity(before_directory, after_directory):
            raise Corruption(label + ": directory changed during snapshot")

    try:
        before_root = os.lstat(path)
        if stat.S_ISLNK(before_root.st_mode) or not stat.S_ISDIR(before_root.st_mode):
            raise Corruption(label + ": root must be a non-symlink directory")
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        root_fd = os.open(path, flags)
        try:
            opened_root = os.fstat(root_fd)
            if not same_identity(before_root, opened_root):
                raise Corruption(label + ": root changed during snapshot")
            visit_directory(root_fd, ".", 0)
            after_root = os.lstat(path)
            if not same_identity(before_root, after_root):
                raise Corruption(label + ": root changed during snapshot")
        finally:
            os.close(root_fd)
    except Corruption:
        raise
    except OSError as exc:
        raise Corruption(label + ": cannot snapshot tree") from exc
    return tuple(rows)


def atomic_publish(path: Path, data: bytes, *, replace_staging: bool = False) -> None:
    require_directory(path.parent, "publish parent")
    old = read_regular(path, max(len(data), MAX_JSON), "existing " + path.name, required=False)
    if old is not None and old == data:
        return
    if old is not None and not replace_staging:
        raise Corruption("immutable file differs: " + str(path))
    temporary = path.parent / ("." + path.name + "." + uuid.uuid4().hex)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC
    fd = os.open(temporary, flags, 0o600)
    try:
        position = 0
        while position < len(data):
            wrote = os.write(fd, data[position:])
            if wrote <= 0:
                raise OSError("short atomic write")
            position += wrote
        os.fsync(fd)
    except BaseException:
        os.close(fd)
        removed = False
        try:
            os.unlink(temporary)
            removed = True
        except FileNotFoundError:
            pass
        if removed:
            fsync_directory(path.parent)
        raise
    else:
        os.close(fd)
    os.replace(temporary, path)
    fsync_directory(path.parent)


def append_json_line(path: Path, value: Any) -> None:
    data = canonical_json(value)
    if len(data) > MAX_JSON:
        raise StoreError("event line exceeds 1 MiB")
    require_directory(path.parent, "log parent")
    if read_regular(path, 64 * MAX_JSON, "existing log", required=False) is not None:
        pass
    flags = os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    fd = os.open(path, flags, 0o600)
    try:
        position = 0
        while position < len(data):
            wrote = os.write(fd, data[position:])
            if wrote <= 0:
                raise OSError("short log append")
            position += wrote
        os.fsync(fd)
    finally:
        os.close(fd)
    fsync_directory(path.parent)


@dataclass(frozen=True)
class TornTail:
    path: Path
    prefix_size: int
    observed_ref: dict[str, Any]


@dataclass(frozen=True)
class LogRead:
    events: tuple[dict[str, Any], ...]
    tail: TornTail | None


def read_json_log(path: Path, label: str, *, required: bool = False) -> LogRead:
    data = read_regular(path, 64 * MAX_JSON, label, required=required)
    if data is None or data == b"":
        return LogRead((), None)
    if data.endswith(b"\n"):
        committed = data[:-1].split(b"\n")
        tail = None
    else:
        pieces = data.split(b"\n")
        committed = pieces[:-1]
        fragment = pieces[-1]
        if len(fragment) > MAX_JSON:
            raise Corruption(label + ": torn tail exceeds 1 MiB")
        tail = TornTail(path, len(data) - len(fragment), make_ref(data))
    events: list[dict[str, Any]] = []
    for index, line in enumerate(committed):
        if not line or len(line) + 1 > MAX_JSON:
            raise Corruption(f"{label}:{index}: invalid committed line size")
        obj = strict_json(line, f"{label}:{index}")
        if not isinstance(obj, dict) or canonical_json(obj) != line + b"\n":
            raise Corruption(f"{label}:{index}: committed line is not canonical")
        events.append(obj)
    return LogRead(tuple(events), tail)


def truncate_verified_tail(tail: TornTail) -> None:
    data = read_regular(tail.path, 64 * MAX_JSON, "torn log")
    assert data is not None
    if make_ref(data) != tail.observed_ref or tail.prefix_size > len(data):
        raise Corruption("torn log changed after validation")
    flags = os.O_WRONLY | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    fd = os.open(tail.path, flags)
    try:
        os.ftruncate(fd, tail.prefix_size)
        os.fsync(fd)
    finally:
        os.close(fd)
    fsync_directory(tail.path.parent)


def is_temp_for(name: str, final: str) -> bool:
    prefix = "." + final + "."
    return name.startswith(prefix) and TEMP_TOKEN_RE.fullmatch(name[len(prefix):]) is not None


@dataclass(frozen=True)
class LockGuard:
    path: Path
    device: int
    inode: int

    def assert_current(self) -> None:
        try:
            info = os.lstat(self.path)
        except OSError as exc:
            raise Corruption("store.lock path changed while locked") from exc
        if (stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode)
                or info.st_dev != self.device or info.st_ino != self.inode):
            raise Corruption("store.lock inode changed while locked")


class StorePaths:
    def __init__(self, raw_root: str | Path):
        raw = os.fspath(raw_root)
        if not os.path.isabs(raw):
            raise StoreError("store path must be lexical absolute")
        self.root = Path(raw)
        self.tasks = self.root / "tasks"
        self.lock_path = self.root / "store.lock"

    def prepare_accept(self) -> None:
        """Create only the unavoidable empty lock skeleton, never authority.

        The root directory and lock inode have to exist before that inode can be
        flocked.  Their creation is the sole bootstrap exception; all semantic
        reads/writes and `tasks/` creation happen after the flock is held.
        """
        _validate_existing_ancestors(self.root)
        require_directory(self.root.parent, "store parent")
        try:
            os.mkdir(self.root, 0o700)
        except FileExistsError:
            pass
        else:
            fsync_directory(self.root.parent)
        require_directory(self.root, "store")
        if not os.path.lexists(self.lock_path) and list_directory(self.root, "pre-lock store"):
            raise Corruption("existing non-empty store is missing store.lock")

    def require_existing(self) -> None:
        _validate_existing_ancestors(self.root)
        require_directory(self.root, "store")

    @contextlib.contextmanager
    def exclusive_lock(self, *, create: bool) -> Iterator[LockGuard]:
        if create:
            self.prepare_accept()
        else:
            self.require_existing()
        flags = os.O_RDWR | os.O_CLOEXEC
        if create:
            flags |= os.O_CREAT
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        try:
            fd = os.open(self.lock_path, flags, 0o600)
        except OSError as exc:
            raise Corruption("store.lock must be a regular non-symlink file") from exc
        try:
            if not stat.S_ISREG(os.fstat(fd).st_mode):
                raise Corruption("store.lock must be regular")
            fcntl.flock(fd, fcntl.LOCK_EX)
            locked = os.fstat(fd)
            guard = LockGuard(self.lock_path, locked.st_dev, locked.st_ino)
            guard.assert_current()
            if create:
                fsync_directory(self.root)
            if create and not self.tasks.exists():
                self.tasks.mkdir()
                fsync_directory(self.root)
            require_directory(self.tasks, "tasks")
            yield guard
        finally:
            try:
                fcntl.flock(fd, fcntl.LOCK_UN)
            finally:
                os.close(fd)
