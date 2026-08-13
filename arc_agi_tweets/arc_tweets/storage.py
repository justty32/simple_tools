from __future__ import annotations

from datetime import datetime, timezone
import json
import os
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Any, Iterable


def load_jsonl(path: Path) -> dict[str, dict[str, Any]]:
    if not path.exists():
        return {}
    posts: dict[str, dict[str, Any]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        try:
            post = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(f"{path} 第 {line_number} 行不是合法 JSON") from exc
        post_id = post.get("id") if isinstance(post, dict) else None
        if not isinstance(post_id, str):
            raise ValueError(f"{path} 第 {line_number} 行缺少字串 id")
        posts[post_id] = post
    return posts


def write_jsonl(path: Path, posts: Iterable[dict[str, Any]]) -> None:
    body = "".join(json.dumps(post, ensure_ascii=False, sort_keys=True) + "\n" for post in posts)
    _atomic_write(path, body)


def load_state(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"sources": {}}
    try:
        state = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"state 檔損壞：{path}") from exc
    if not isinstance(state, dict) or not isinstance(state.get("sources", {}), dict):
        raise ValueError(f"state 格式錯誤：{path}")
    state.setdefault("sources", {})
    return state


def write_state(path: Path, state: dict[str, Any]) -> None:
    state["updated_at"] = datetime.now(timezone.utc).isoformat()
    _atomic_write(path, json.dumps(state, ensure_ascii=False, indent=2, sort_keys=True) + "\n")


def write_markdown(
    path: Path,
    posts: list[dict[str, Any]],
    *,
    min_score: float,
    max_items: int,
) -> int:
    selected = [post for post in posts if float(post.get("score", 0)) >= min_score]
    selected.sort(key=lambda post: (float(post.get("score", 0)), post.get("created_at", "")), reverse=True)
    selected = selected[:max_items]
    generated = datetime.now(timezone.utc).isoformat(timespec="seconds")
    lines = [
        "# ARC-AGI 高訊號推文\n",
        f"> 產生時間：{generated}｜門檻：{min_score:g}｜共 {len(selected)} 筆\n",
    ]
    for index, post in enumerate(selected, start=1):
        author = post.get("author", {})
        username = author.get("username") or "unknown"
        name = author.get("name") or username
        created_at = str(post.get("created_at", ""))[:10] or "時間未知"
        score = float(post.get("score", 0))
        text = str(post.get("text", "")).strip().replace("\r\n", "\n")
        metrics = post.get("metrics", {})
        sources = ", ".join(post.get("source_names", []))
        reasons = "、".join(post.get("score_reasons", [])) or "—"
        lines.extend(
            [
                f"## {index}. {name} (@{username}) · {score:.2f} 分 · {created_at}\n",
                f"{text}\n",
                (
                    f"[查看原文]({post.get('url', '')}) · "
                    f"👍 {metrics.get('like_count', 0)} · "
                    f"🔁 {metrics.get('retweet_count', 0)} · "
                    f"💬 {metrics.get('reply_count', 0)}\n"
                ),
                f"入選原因：{reasons}  \n來源：{sources}\n",
            ]
        )
    _atomic_write(path, "\n".join(lines).rstrip() + "\n")
    return len(selected)


def _atomic_write(path: Path, body: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: str | None = None
    try:
        with NamedTemporaryFile(
            "w",
            encoding="utf-8",
            newline="\n",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as handle:
            temporary = handle.name
            handle.write(body)
        os.replace(temporary, path)
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)
