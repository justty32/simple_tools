from __future__ import annotations

from pathlib import Path
import unittest

from arc_tweets.config import load_settings, validate_for_endpoint


ROOT = Path(__file__).resolve().parents[1]


class ConfigTests(unittest.TestCase):
    def test_shipped_config_is_valid_for_recent_and_archive(self) -> None:
        settings = load_settings(ROOT / "config.toml")

        validate_for_endpoint(settings, archive=False, page_size=settings.page_size)
        validate_for_endpoint(settings, archive=True, page_size=settings.page_size)

        self.assertIn("fchollet", settings.trusted_authors)
        self.assertIn("richardssutton", settings.trusted_authors)
        self.assertIn("wolfrein", settings.trusted_authors)
        self.assertEqual(len(settings.sources), 3)

    def test_recent_endpoint_rejects_archive_sized_page(self) -> None:
        settings = load_settings(ROOT / "config.toml")

        with self.assertRaisesRegex(ValueError, "100"):
            validate_for_endpoint(settings, archive=False, page_size=101)


if __name__ == "__main__":
    unittest.main()
