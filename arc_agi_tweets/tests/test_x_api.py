from __future__ import annotations

import json
import unittest
from urllib.error import HTTPError
from io import BytesIO
from urllib.parse import parse_qs, urlparse

from arc_tweets.x_api import XAPIError, XClient


class FakeResponse:
    def __init__(self, payload: dict, headers: dict[str, str] | None = None) -> None:
        self._body = json.dumps(payload).encode("utf-8")
        self.headers = headers or {}

    def read(self) -> bytes:
        return self._body

    def __enter__(self) -> "FakeResponse":
        return self

    def __exit__(self, *_: object) -> None:
        return None


class XClientTests(unittest.TestCase):
    def test_recent_search_paginates_and_passes_since_id(self) -> None:
        requests = []
        responses = [
            FakeResponse({"data": [{"id": "2"}], "meta": {"next_token": "next"}}),
            FakeResponse({"data": [{"id": "1"}], "meta": {}}),
        ]

        def opener(request, *, timeout):
            requests.append((request, timeout))
            return responses.pop(0)

        client = XClient("secret-token", timeout=12, opener=opener)
        pages = list(
            client.search(
                '"ARC-AGI" -is:retweet',
                archive=False,
                page_size=10,
                max_pages=2,
                since_id="123",
            )
        )

        self.assertEqual(len(pages), 2)
        first_query = parse_qs(urlparse(requests[0][0].full_url).query)
        second_query = parse_qs(urlparse(requests[1][0].full_url).query)
        self.assertEqual(first_query["since_id"], ["123"])
        self.assertNotIn("next_token", first_query)
        self.assertEqual(second_query["next_token"], ["next"])
        self.assertEqual(requests[0][1], 12)
        self.assertNotIn("secret-token", requests[0][0].full_url)

    def test_http_error_does_not_leak_token(self) -> None:
        def opener(request, *, timeout):
            raise HTTPError(
                request.full_url,
                401,
                "Unauthorized",
                {},
                BytesIO(b'{"title":"Unauthorized"}'),
            )

        client = XClient("super-secret-token", opener=opener)
        with self.assertRaises(XAPIError) as caught:
            list(
                client.search(
                    '"ARC-AGI"',
                    archive=False,
                    page_size=10,
                    max_pages=1,
                )
            )

        self.assertIn("HTTP 401", str(caught.exception))
        self.assertNotIn("super-secret-token", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
