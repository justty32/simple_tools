from __future__ import annotations

from dataclasses import dataclass
import json
from typing import Any, Callable, Iterator
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


RECENT_ENDPOINT = "https://api.x.com/2/tweets/search/recent"
ARCHIVE_ENDPOINT = "https://api.x.com/2/tweets/search/all"


class XAPIError(RuntimeError):
    """A safe-to-display X API error that never includes the bearer token."""


@dataclass(frozen=True)
class SearchPage:
    payload: dict[str, Any]
    rate_limit_remaining: str | None
    rate_limit_reset: str | None


class XClient:
    def __init__(
        self,
        bearer_token: str,
        *,
        timeout: int = 30,
        opener: Callable[..., Any] = urlopen,
    ) -> None:
        if not bearer_token.strip():
            raise ValueError("X_BEARER_TOKEN 不可為空")
        self._token = bearer_token.strip()
        self._timeout = timeout
        self._opener = opener

    def search(
        self,
        query: str,
        *,
        archive: bool,
        page_size: int,
        max_pages: int,
        since_id: str | None = None,
        start_time: str | None = None,
        end_time: str | None = None,
    ) -> Iterator[SearchPage]:
        endpoint = ARCHIVE_ENDPOINT if archive else RECENT_ENDPOINT
        params = {
            "query": query,
            "max_results": str(page_size),
            "tweet.fields": (
                "author_id,conversation_id,created_at,entities,lang,note_tweet,"
                "public_metrics,referenced_tweets"
            ),
            "expansions": "author_id",
            "user.fields": "name,username,verified",
        }
        if since_id and not archive:
            params["since_id"] = since_id
        if start_time:
            params["start_time"] = start_time
        if end_time:
            params["end_time"] = end_time

        next_token: str | None = None
        for _ in range(max_pages):
            if next_token:
                params["next_token"] = next_token
            request = Request(
                f"{endpoint}?{urlencode(params)}",
                headers={
                    "Authorization": f"Bearer {self._token}",
                    "User-Agent": "arc-agi-tweets/0.1",
                    "Accept": "application/json",
                },
            )
            try:
                with self._opener(request, timeout=self._timeout) as response:
                    payload = json.loads(response.read().decode("utf-8"))
                    headers = response.headers
            except HTTPError as exc:
                raise _http_error(exc) from exc
            except URLError as exc:
                raise XAPIError(f"無法連到 X API：{exc.reason}") from exc
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise XAPIError("X API 回傳了無法解析的 JSON") from exc

            if not isinstance(payload, dict):
                raise XAPIError("X API 回傳格式不是 JSON object")
            yield SearchPage(
                payload=payload,
                rate_limit_remaining=headers.get("x-rate-limit-remaining"),
                rate_limit_reset=headers.get("x-rate-limit-reset"),
            )
            meta = payload.get("meta", {})
            next_token = meta.get("next_token") if isinstance(meta, dict) else None
            if not next_token:
                break


def _http_error(exc: HTTPError) -> XAPIError:
    try:
        body = exc.read().decode("utf-8", errors="replace")
        parsed = json.loads(body)
        detail = parsed.get("detail") or parsed.get("title") or body
    except (json.JSONDecodeError, AttributeError):
        detail = ""
    reset = exc.headers.get("x-rate-limit-reset") if exc.headers else None
    suffix = f"；rate limit reset={reset}" if exc.code == 429 and reset else ""
    return XAPIError(f"X API HTTP {exc.code}：{detail or exc.reason}{suffix}")
