from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import tomllib


@dataclass(frozen=True)
class Source:
    name: str
    description: str
    query: str


@dataclass(frozen=True)
class Settings:
    page_size: int
    max_pages: int
    min_score: float
    max_curated: int
    request_timeout_seconds: int
    trusted_authors: frozenset[str]
    sources: tuple[Source, ...]


def load_settings(path: Path) -> Settings:
    try:
        raw = tomllib.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"找不到設定檔：{path}") from exc
    except tomllib.TOMLDecodeError as exc:
        raise ValueError(f"設定檔 TOML 格式錯誤：{exc}") from exc

    collector = raw.get("collector")
    if not isinstance(collector, dict):
        raise ValueError("設定檔缺少 [collector]")

    source_rows = raw.get("sources")
    if not isinstance(source_rows, list) or not source_rows:
        raise ValueError("設定檔至少需要一個 [[sources]]")

    sources: list[Source] = []
    names: set[str] = set()
    for index, row in enumerate(source_rows, start=1):
        if not isinstance(row, dict):
            raise ValueError(f"第 {index} 個 source 必須是 TOML table")
        name = _required_string(row, "name", f"sources[{index}]")
        description = _required_string(row, "description", f"sources[{index}]")
        query = _required_string(row, "query", f"sources[{index}]")
        if name in names:
            raise ValueError(f"source 名稱重複：{name}")
        names.add(name)
        sources.append(Source(name=name, description=description, query=query))

    page_size = _integer(collector, "page_size", 25)
    max_pages = _integer(collector, "max_pages", 1)
    max_curated = _integer(collector, "max_curated", 100)
    timeout = _integer(collector, "request_timeout_seconds", 30)
    min_score = collector.get("min_score", 7.0)
    if not isinstance(min_score, (int, float)):
        raise ValueError("collector.min_score 必須是數字")
    if not 10 <= page_size <= 500:
        raise ValueError("collector.page_size 必須介於 10 與 500")
    if max_pages < 1 or max_curated < 1 or timeout < 1:
        raise ValueError("max_pages、max_curated、request_timeout_seconds 必須大於 0")

    trusted = collector.get("trusted_authors", [])
    if not isinstance(trusted, list) or not all(isinstance(item, str) for item in trusted):
        raise ValueError("collector.trusted_authors 必須是字串陣列")

    return Settings(
        page_size=page_size,
        max_pages=max_pages,
        min_score=float(min_score),
        max_curated=max_curated,
        request_timeout_seconds=timeout,
        trusted_authors=frozenset(item.lower().lstrip("@") for item in trusted),
        sources=tuple(sources),
    )


def validate_for_endpoint(settings: Settings, *, archive: bool, page_size: int) -> None:
    maximum_page_size = 500 if archive else 100
    maximum_query_length = 1024 if archive else 512
    if not 10 <= page_size <= maximum_page_size:
        mode = "完整歷史" if archive else "近期"
        raise ValueError(f"{mode}搜尋的 page size 必須介於 10 與 {maximum_page_size}")
    for source in settings.sources:
        if len(source.query) > maximum_query_length:
            raise ValueError(
                f"{source.name} query 有 {len(source.query)} 字元，"
                f"超過此 endpoint 的 {maximum_query_length} 字元限制"
            )


def _required_string(row: dict[str, object], key: str, location: str) -> str:
    value = row.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{location}.{key} 必須是非空字串")
    return value.strip()


def _integer(row: dict[str, object], key: str, default: int) -> int:
    value = row.get(key, default)
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"collector.{key} 必須是整數")
    return value
