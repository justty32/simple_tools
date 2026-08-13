from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from arc_tweets.collector import collect
from arc_tweets.config import Settings, Source
from arc_tweets.x_api import SearchPage


class FakeClient:
    def search(self, query, **kwargs):
        yield SearchPage(
            payload={
                "data": [
                    {
                        "id": "200",
                        "author_id": "10",
                        "text": "short fallback",
                        "note_tweet": {
                            "text": "ARC-AGI technical report with benchmark evaluation results and source code.",
                            "entities": {
                                "urls": [{"expanded_url": "https://github.com/example/arc-solver"}]
                            },
                        },
                        "created_at": "2026-08-13T01:00:00.000Z",
                        "public_metrics": {"like_count": 10, "retweet_count": 2, "reply_count": 1},
                    }
                ],
                "includes": {"users": [{"id": "10", "username": "fchollet", "name": "François"}]},
                "meta": {"newest_id": "200"},
            },
            rate_limit_remaining="99",
            rate_limit_reset=None,
        )


class CollectorTests(unittest.TestCase):
    def test_collect_writes_deduplicated_jsonl_markdown_and_state(self) -> None:
        settings = Settings(
            page_size=10,
            max_pages=1,
            min_score=7.0,
            max_curated=10,
            request_timeout_seconds=10,
            trusted_authors=frozenset({"fchollet"}),
            sources=(Source("topic", "topic", '"ARC-AGI"'),),
        )
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            first = collect(
                FakeClient(),
                settings,
                output_dir=output,
                archive=False,
                page_size=10,
                max_pages=1,
                min_score=7.0,
                start_time=None,
                end_time=None,
            )
            second = collect(
                FakeClient(),
                settings,
                output_dir=output,
                archive=False,
                page_size=10,
                max_pages=1,
                min_score=7.0,
                start_time=None,
                end_time=None,
            )

            self.assertEqual(first.new, 1)
            self.assertEqual(second.new, 0)
            self.assertEqual(len((output / "tweets.jsonl").read_text(encoding="utf-8").splitlines()), 1)
            markdown = (output / "curated.md").read_text(encoding="utf-8")
            self.assertIn("ARC-AGI technical report", markdown)
            self.assertIn("https://x.com/fchollet/status/200", markdown)
            self.assertIn('"since_id": "200"', (output / ".state.json").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
