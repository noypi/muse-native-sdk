"""In-memory HTTP fixture map (FR-TESTING-002 / AC-TESTING-015).

Responses are replayed from a per-instance dict keyed by path; no
live I/O is performed and unknown paths fail closed with ``KeyError``.
Stdlib only.
"""


class MockHttpServer:
    """Map of enqueued fixture bodies, independent per instance."""

    def __init__(self) -> None:
        self._fixtures: dict[str, bytes] = {}

    def enqueue_fixture(self, path: str, body: bytes) -> None:
        """Record ``body`` for ``path``; re-enqueueing replaces it."""
        self._fixtures[path] = body

    def handle_request(self, path: str) -> bytes:
        """Return the exact bytes enqueued for ``path``.

        Raises:
            KeyError: if nothing was enqueued for ``path``.
        """
        return self._fixtures[path]
