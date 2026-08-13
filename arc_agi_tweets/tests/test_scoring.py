from __future__ import annotations

import unittest

from arc_tweets.scoring import score_post


class ScoringTests(unittest.TestCase):
    def test_technical_first_party_post_outranks_hype(self) -> None:
        technical = {
            "text": (
                "ARC-AGI-2 technical report: our solver uses test-time program synthesis. "
                "We include ablations, evaluation results, and source code for reproduction."
            ),
            "author": {"username": "researcher"},
            "links": ["https://arxiv.org/abs/2505.11831", "https://github.com/example/solver"],
            "metrics": {"like_count": 25, "reply_count": 4, "retweet_count": 6},
        }
        hype = {
            "text": "BREAKING: ARC-AGI GAME OVER! AGI ACHIEVED! This changes everything!",
            "author": {"username": "hype"},
            "links": [],
            "metrics": {"like_count": 1000, "reply_count": 20, "retweet_count": 100},
        }

        technical_score, _ = score_post(technical, [])
        hype_score, _ = score_post(hype, [])

        self.assertGreater(technical_score, hype_score)
        self.assertGreaterEqual(technical_score, 10)

    def test_trusted_author_bonus_is_case_insensitive(self) -> None:
        post = {
            "text": "A careful note about intelligence and generalization in benchmark evaluation.",
            "author": {"username": "FChollet"},
            "links": [],
            "metrics": {},
        }
        trusted_score, reasons = score_post(post, ["fchollet"])
        other_score, _ = score_post(post, [])

        self.assertEqual(trusted_score - other_score, 4.0)
        self.assertIn("可信／指定作者", reasons)


if __name__ == "__main__":
    unittest.main()
