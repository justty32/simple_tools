from __future__ import annotations

import math
import re
from typing import Any, Iterable
from urllib.parse import urlparse


ARC_TERMS = (
    "arc-agi",
    "arc agi",
    "arc_agi",
    "abstraction and reasoning corpus",
)

TECHNICAL_TERMS = (
    "abstraction",
    "reasoning",
    "generalization",
    "generalisation",
    "program synthesis",
    "test-time",
    "test time",
    "induction",
    "world model",
    "object-centric",
    "neuro-symbolic",
    "few-shot",
    "solver",
    "benchmark",
    "evaluation",
    "contamination",
    "efficiency",
    "fluid intelligence",
    "sample efficiency",
    "continual learning",
    "representation",
    "search algorithm",
    "推理",
    "抽象",
    "泛化",
    "基準",
)

EVIDENCE_TERMS = (
    "paper",
    "preprint",
    "technical report",
    "source code",
    "github",
    "dataset",
    "results",
    "experiment",
    "ablation",
    "implementation",
    "論文",
    "原始碼",
    "實驗",
    "結果",
)

EVIDENCE_DOMAINS = {
    "arxiv.org",
    "github.com",
    "openreview.net",
    "arcprize.org",
    "kaggle.com",
    "paperswithcode.com",
}

HYPE_TERMS = (
    "game over",
    "agi achieved",
    "this changes everything",
    "you won't believe",
    "breaking:",
    "insane!",
    "100x",
    "guaranteed",
    "震撼",
    "顛覆一切",
)

PROMO_TERMS = ("giveaway", "airdrop", "use my code", "limited offer", "免費領取")


def score_post(post: dict[str, Any], trusted_authors: Iterable[str]) -> tuple[float, list[str]]:
    text = str(post.get("text", ""))
    lowered = text.casefold()
    reasons: list[str] = []
    score = 0.0

    username = str(post.get("author", {}).get("username", "")).lower()
    trusted = {author.lower().lstrip("@") for author in trusted_authors}
    if username in trusted:
        score += 4.0
        reasons.append("可信／指定作者")

    arc_hits = sum(term in lowered for term in ARC_TERMS)
    if arc_hits:
        score += 5.0 + min(arc_hits - 1, 2) * 0.75
        reasons.append("直接討論 ARC-AGI")

    technical_hits = _unique_hits(lowered, TECHNICAL_TERMS)
    if technical_hits:
        score += min(len(technical_hits) * 0.9, 6.3)
        reasons.append(f"技術訊號 {len(technical_hits)} 項")

    evidence_hits = _unique_hits(lowered, EVIDENCE_TERMS)
    if evidence_hits:
        score += min(len(evidence_hits) * 0.75, 3.0)
        reasons.append("含研究／實作證據")

    domains = _link_domains(post.get("links", []))
    strong_domains = sorted(domains & EVIDENCE_DOMAINS)
    if strong_domains:
        score += 3.0
        reasons.append(f"一手資料連結：{', '.join(strong_domains)}")
    elif domains:
        score += 0.5

    length = len(text.strip())
    if length >= 400:
        score += 2.5
        reasons.append("內容完整")
    elif length >= 180:
        score += 1.5
        reasons.append("有實質說明")
    elif length >= 80:
        score += 0.5
    elif length < 45:
        score -= 2.0

    metrics = post.get("metrics", {})
    likes = _nonnegative_number(metrics.get("like_count", 0))
    discussion = sum(
        _nonnegative_number(metrics.get(key, 0))
        for key in ("reply_count", "quote_count", "retweet_count", "bookmark_count")
    )
    engagement_bonus = min(math.log10(1 + likes) * 0.7 + math.log10(1 + discussion) * 0.8, 4.0)
    if engagement_bonus >= 1.0:
        reasons.append("有一定互動／討論")
    score += engagement_bonus

    hype_hits = _unique_hits(lowered, HYPE_TERMS)
    if hype_hits:
        score -= min(len(hype_hits) * 2.0, 6.0)
        reasons.append("疑似誇張標題（扣分）")
    promo_hits = _unique_hits(lowered, PROMO_TERMS)
    if promo_hits:
        score -= 5.0
        reasons.append("宣傳訊號（扣分）")

    return round(max(score, 0.0), 2), reasons


def _unique_hits(text: str, terms: Iterable[str]) -> set[str]:
    return {term for term in terms if term in text}


def _link_domains(links: object) -> set[str]:
    if not isinstance(links, list):
        return set()
    domains: set[str] = set()
    for link in links:
        if not isinstance(link, str):
            continue
        hostname = (urlparse(link).hostname or "").lower()
        hostname = re.sub(r"^www\.", "", hostname)
        if hostname:
            domains.add(hostname)
    return domains


def _nonnegative_number(value: object) -> float:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return max(float(value), 0.0)
    return 0.0
