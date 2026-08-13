"""Read JSON files and recursively resolve ``$ref`` and ``$env`` nodes."""

from dataclasses import dataclass
import json
import os
from pathlib import Path

from . import jsref


def read(path: str | os.PathLike[str]) -> object:
    source = Path(path).absolute()
    resolver = _Resolver()
    root = resolver.load(source)
    return resolver.node(root, _Document(root, source))


def resolve_in(value: object, base_dir: str | os.PathLike[str]) -> object:
    resolver = _Resolver()
    return resolver.node(value, _Document(value, None, Path(base_dir).absolute()))


@dataclass(frozen=True)
class _Document:
    root: object
    path: Path | None
    directory: Path

    def __init__(
        self,
        root: object,
        path: Path | None,
        directory: Path | None = None,
    ) -> None:
        object.__setattr__(self, "root", root)
        object.__setattr__(self, "path", path)
        object.__setattr__(
            self,
            "directory",
            directory if directory is not None else path.parent,
        )


class _Resolver:
    def __init__(self) -> None:
        self.cache: dict[Path, object] = {}
        self.active: set[str] = set()

    def load(self, path: Path) -> object:
        path = path.absolute()
        if path not in self.cache:
            with path.open(encoding="utf-8") as source:
                self.cache[path] = json.load(source)
        return self.cache[path]

    def node(self, value: object, document: _Document) -> object:
        if isinstance(value, dict):
            if "$ref" in value:
                return self.ref(value["$ref"], document)
            if "$env" in value:
                name = value["$env"]
                if not isinstance(name, str):
                    raise ValueError("jsonx $env 必須是字串")
                result = os.environ.get(name)
                return value.get("default") if result in (None, "") else result
            return {key: self.node(item, document) for key, item in value.items()}
        if isinstance(value, list):
            return [self.node(item, document) for item in value]
        return value

    def ref(self, raw: object, document: _Document) -> object:
        if isinstance(raw, str):
            return self.one(raw, document)
        if not isinstance(raw, list):
            raise ValueError("jsonx $ref 必須是字串或字串陣列")
        if not raw:
            raise ValueError("jsonx $ref 陣列不可為空")
        result: object = None
        for index, item in enumerate(raw):
            if not isinstance(item, str):
                raise ValueError(f"jsonx $ref 陣列第 {index} 項必須是字串")
            resolved = self.one(item, document)
            result = resolved if index == 0 else jsref.merge(result, resolved)
        return result

    def one(self, ref: str, document: _Document) -> object:
        file, fragment = jsref.split_ref(ref)
        if file:
            target = Path(file)
            if not target.is_absolute():
                target = document.directory / target
            target = target.absolute()
            root = self.load(target)
            next_document = _Document(root, target)
        else:
            target = document.path
            root = document.root
            next_document = document

        key = f"{target or '<memory>'}#{fragment}"
        if key in self.active:
            raise ValueError(f"jsonx $ref 迴圈：{key}")
        value, found = jsref.lookup(root, fragment)
        if not found:
            raise ValueError(f"jsonx $ref 找不到路徑：{ref}")
        self.active.add(key)
        try:
            return self.node(value, next_document)
        finally:
            self.active.remove(key)
