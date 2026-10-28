"""Seeded PRNG and pinned execution descriptors (FR-TESTING-002).

:class:`SeedablePRNG` replays an identical integer sequence per seed
via a small self-contained generator (no OS entropy). Every
randomized-failure message embeds ``seed=<int>`` so failures are
reproducible. :func:`pinned_environment` exposes fixed
locale/timezone/fonts/display descriptors per seed and restores the
host environment on exit. Stdlib only.
"""

import contextlib
import os
from typing import Dict, Iterator

# Randomized-failure output always carries its seed for reproducibility.
FAILURE_TEMPLATE = "randomized failure seed={seed}: {detail}"


def format_failure(detail: str, seed: int) -> str:
    """Render a randomized-failure message containing ``seed=<int>``."""
    return FAILURE_TEMPLATE.format(seed=seed, detail=detail)


_LOCALES = ("en_US.UTF-8", "de_DE.UTF-8", "ja_JP.UTF-8")
_TIMEZONES = ("UTC", "America/New_York", "Asia/Tokyo")
_FONT_SETS = (
    ("DejaVu Sans", "Noto Sans"),
    ("Noto Sans", "Noto Sans Mono"),
    ("DejaVu Sans", "DejaVu Sans Mono"),
)
_DISPLAYS = ("800x600@60", "1280x720@60", "1920x1080@60")


class SeedablePRNG:
    """Deterministic integer generator seeded once at construction."""

    _MODULUS = 2**32
    _MULTIPLIER = 1664525
    _INCREMENT = 1013904223

    def __init__(self, seed: int) -> None:
        self._seed = seed
        self._state = seed & 0xFFFFFFFF

    @property
    def seed(self) -> int:
        """The seed this generator was constructed with."""
        return self._seed

    def next_int(self, bound: int) -> int:
        """Return the next draw in ``[0, bound)``.

        Raises:
            ValueError: if ``bound`` is not positive.
        """
        if bound <= 0:
            raise ValueError(
                "next_int requires a positive bound, seed={seed}".format(
                    seed=self._seed
                )
            )
        self._state = (
            (self._MULTIPLIER * self._state + self._INCREMENT) % self._MODULUS
        )
        if bound == 1:
            return 0
        return self._state % bound


@contextlib.contextmanager
def pinned_environment(seed: int) -> Iterator[Dict[str, object]]:
    """Pin locale/timezone/font-set/display descriptors for ``seed``.

    Yields a mapping with ``locale``/``timezone``/``fonts``/``display``
    keys that is identical for repeated entries with the same seed.
    Any host-environment mutation observed on exit is rolled back.
    """
    rng = SeedablePRNG(seed)
    descriptors: Dict[str, object] = {
        "locale": _LOCALES[rng.next_int(len(_LOCALES))],
        "timezone": _TIMEZONES[rng.next_int(len(_TIMEZONES))],
        "fonts": _FONT_SETS[rng.next_int(len(_FONT_SETS))],
        "display": _DISPLAYS[rng.next_int(len(_DISPLAYS))],
    }
    saved = dict(os.environ)
    os.environ["LC_ALL"] = str(descriptors["locale"])
    os.environ["TZ"] = str(descriptors["timezone"])
    try:
        yield descriptors
    finally:
        os.environ.clear()
        os.environ.update(saved)


__all__ = [
    "FAILURE_TEMPLATE",
    "SeedablePRNG",
    "format_failure",
    "pinned_environment",
]
