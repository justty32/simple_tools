from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .config import Settings, Source
from .scoring import score_post
from .storage import load_jsonl, load_state, write_jsonl, write_markdown, write_state
from .x_api import XAPIError, XClient


@dataclass
class CollectionSummary:
    fetched: int = 0
    new: int = 0
    total: int = 0
    curated: int = 0
    requests: int = 0
    source_counts: dict[str, int] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)
    rate_limit_remaining: str | None = None


def collect(
    client: XClient,
    settings: Settings,
    *,
    output_dir: Path,
    archive: bool,
    page_size: int,
    max_pages: int,
    min_score: float,
    start_time: str | None,
    end_time: str | None,
) -> CollectionSummary:
    output_dir.mkdir(parents=True, exist_ok=True)
    jsonl_path = output_dir / "tweets.jsonl"
    markdown_path = output_dir / "curated.md"
    state_path = output_dir / ".state.json"
    existing = load_jsonl(jsonl_path)
    state = load_state(state_path)
    summary = CollectionSummary()
    collected_at = datetime.now(timezone.utc).isoformat()

    for source in settings.sources:
        source_state = state["sources"].get(source.name, {})
        since_id = None
        if not archive and source_state.get("query") == source.query:
            since_id = source_state.get("since_id")
        newest_id: str | None = None
        fetched_for_source = 0
        try:
            for page in client.search(
                source.query,
                archive=archive,
                page_size=page_size,
                max_pages=max_pages,
                since_id=since_id,
                start_time=start_time,
                end_time=end_time,
            ):
                summary.requests += 1
                summary.rate_limit_remaining = page.rate_limit_remaining
                posts = _normalise_page(page.payload, source, collected_at)
                fetched_for_source += len(posts)
                for post in posts:
                    post_id = post["id"]
                    if newest_id is None or int(post_id) > int(newest_id):
                        newest_id = post_id
                    prior = existing.get(post_id)
                    if prior:
                        post["first_seen_at"] = prior.get("first_seen_at", prior.get("collected_at", collected_at))
                        post["source_names"] = sorted(
                            set(prior.get("source_names", [])) | set(post["source_names"])
                        )
                    else:
                        summary.new += 1
                    existing[post_id] = post
        except XAPIError as exc:
            summary.errors.append(f"{source.name}: {exc}")
            continue

        summary.fetched += fetched_for_source
        summary.source_counts[source.name] = fetched_for_source
        if not archive:
            state["sources"][source.name] = {
                "query": source.query,
                "since_id": newest_id or since_id,
            }

    posts = list(existing.values())
    for post in posts:
        post["score"], post["score_reasons"] = score_post(post, settings.trusted_authors)
    posts.sort(key=lambda post: (post.get("created_at", ""), post.get("id", "")), reverse=True)
    write_jsonl(jsonl_path, posts)
    summary.curated = write_markdown(
        markdown_path,
        posts,
        min_score=min_score,
        max_items=settings.max_curated,
    )
    if not archive:
        write_state(state_path, state)
    summary.total = len(posts)
    return summary


def _normalise_page(payload: dict[str, Any], source: Source, collected_at: str) -> list[dict[str, Any]]:
    users = {
        str(user.get("id")): user
        for user in payload.get("includes", {}).get("users", [])
        if isinstance(user, dict) and user.get("id")
    }
    posts: list[dict[str, Any]] = []
    for raw in payload.get("data", []) or []:
        if not isinstance(raw, dict) or not raw.get("id"):
            continue
        author = users.get(str(raw.get("author_id")), {})
        username = str(author.get("username") or raw.get("author_id") or "unknown")
        note_tweet = raw.get("note_tweet") if isinstance(raw.get("note_tweet"), dict) else {}
        text = str(note_tweet.get("text") or raw.get("text") or "")
        entities = note_tweet.get("entities") or raw.get("entities") or {}
        urls = entities.get("urls", []) if isinstance(entities, dict) else []
        links = []
        for row in urls:
            if isinstance(row, dict):
                url = row.get("unwound_url") or row.get("expanded_url") or row.get("url")
                if isinstance(url, str):
                    links.append(url)
        metrics = raw.get("public_metrics") if isinstance(raw.get("public_metrics"), dict) else {}
        post_id = str(raw["id"])
        posts.append(
            {
                "id": post_id,
                "text": text,
                "created_at": raw.get("created_at"),
                "lang": raw.get("lang"),
                "author": {
                    "id": str(author.get("id") or raw.get("author_id") or ""),
                    "username": username,
                    "name": author.get("name") or username,
                    "verified": bool(author.get("verified", False)),
                },
                "metrics": metrics,
                "links": sorted(set(links)),
                "url": f"https://x.com/{username}/status/{post_id}",
                "conversation_id": raw.get("conversation_id"),
                "referenced_tweets": raw.get("referenced_tweets", []),
                "source_names": [source.name],
                "first_seen_at": collected_at,
                "last_seen_at": collected_at,
            }
        )
    return posts
