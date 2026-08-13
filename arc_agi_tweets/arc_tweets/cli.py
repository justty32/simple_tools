from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys

from .collector import collect
from .config import Settings, load_settings, validate_for_endpoint
from .x_api import XClient


# 只用於執行前的保守估算；實際價格以 X Developer Console 為準。
ESTIMATED_USD_PER_POST_READ = 0.005


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="arc-tweets",
        description="抓取並篩選高訊號 ARC-AGI X/Twitter 推文",
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=Path("config.toml"),
        help="TOML 設定檔（預設：config.toml）",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    plan = subparsers.add_parser("plan", help="顯示 query 與最高費用估算，不連線")
    _add_search_options(plan, include_output=False)

    fetch = subparsers.add_parser("fetch", help="執行抓取；沒有 --execute 時只預覽")
    _add_search_options(fetch, include_output=True)
    fetch.add_argument(
        "--execute",
        action="store_true",
        help="確認真的呼叫會計費的 X API",
    )
    return parser


def _add_search_options(parser: argparse.ArgumentParser, *, include_output: bool) -> None:
    parser.add_argument("--archive", action="store_true", help="使用完整歷史搜尋 endpoint")
    parser.add_argument("--page-size", type=int, help="每頁最多抓取筆數")
    parser.add_argument("--pages", type=int, help="每個 source 最多頁數")
    parser.add_argument("--start-time", help="UTC RFC3339，例如 2025-01-01T00:00:00Z")
    parser.add_argument("--end-time", help="UTC RFC3339，例如 2025-02-01T00:00:00Z")
    if include_output:
        parser.add_argument("--output-dir", type=Path, default=Path("data"))
        parser.add_argument("--min-score", type=float, help="精選輸出的最低分數")


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        settings = load_settings(args.config)
        page_size = args.page_size or settings.page_size
        pages = args.pages or settings.max_pages
        if pages < 1:
            raise ValueError("--pages 必須大於 0")
        validate_for_endpoint(settings, archive=args.archive, page_size=page_size)
    except ValueError as exc:
        print(f"設定錯誤：{exc}", file=sys.stderr)
        return 2

    _print_plan(settings, archive=args.archive, page_size=page_size, pages=pages)
    if args.command == "plan" or not args.execute:
        if args.command == "fetch":
            print("\n尚未連線：加上 --execute 才會實際呼叫 X API。")
        return 0

    token = os.environ.get("X_BEARER_TOKEN", "")
    if not token.strip():
        print("缺少 X_BEARER_TOKEN 環境變數；沒有發出任何 API request。", file=sys.stderr)
        return 2

    min_score = args.min_score if args.min_score is not None else settings.min_score
    client = XClient(token, timeout=settings.request_timeout_seconds)
    try:
        summary = collect(
            client,
            settings,
            output_dir=args.output_dir,
            archive=args.archive,
            page_size=page_size,
            max_pages=pages,
            min_score=min_score,
            start_time=args.start_time,
            end_time=args.end_time,
        )
    except (OSError, ValueError) as exc:
        print(f"寫入／資料錯誤：{exc}", file=sys.stderr)
        return 1


    print(
        f"\n完成：{summary.requests} requests，取得 {summary.fetched} 筆，"
        f"新增 {summary.new} 筆，累計 {summary.total} 筆，精選 {summary.curated} 筆。"
    )
    for source_name, count in summary.source_counts.items():
        print(f"  {source_name}: {count}")
    if summary.rate_limit_remaining is not None:
        print(f"X rate-limit remaining: {summary.rate_limit_remaining}")
    if summary.errors:
        print("部分 source 失敗：", file=sys.stderr)
        for error in summary.errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"資料：{args.output_dir / 'tweets.jsonl'}")
    print(f"精選：{args.output_dir / 'curated.md'}")
    return 0


def _print_plan(settings: Settings, *, archive: bool, page_size: int, pages: int) -> None:
    mode = "完整歷史" if archive else "最近 7 天"
    maximum_posts = len(settings.sources) * page_size * pages
    estimate = maximum_posts * ESTIMATED_USD_PER_POST_READ
    print(f"模式：{mode}")
    print(f"sources：{len(settings.sources)}｜每 source：{page_size} × {pages} 頁")
    print(f"最多讀取：{maximum_posts} Posts｜價格上限估算：約 US${estimate:.3f}")
    print("（估算採 US$0.005/Post；實際費用一律以 X Developer Console 為準。）")
    for source in settings.sources:
        print(f"\n[{source.name}] {source.description}\n{source.query}")


if __name__ == "__main__":
    raise SystemExit(main())
