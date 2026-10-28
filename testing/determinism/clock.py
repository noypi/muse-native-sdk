"""Single virtual timeline (FR-TESTING-002 / AC-TESTING-016).

The render loop and the JS event callback both advance from one
:class:`VirtualClock`; stepping N vsyncs moves ``now()`` by exactly
``N * vsync_ms``. Stdlib only.
"""

from typing import Callable


class VirtualClock:
    """Virtual millisecond clock with a fixed vsync period."""

    def __init__(self, now_ms: int = 0, vsync_ms: int = 16) -> None:
        self._now = now_ms
        self._vsync = vsync_ms

    def now(self) -> int:
        """Return the current virtual time in milliseconds."""
        return self._now

    def advance_ms(self, ms: int) -> int:
        """Move the clock forward by ``ms``; return the new ``now()``."""
        if ms < 0:
            raise ValueError("advance_ms requires a non-negative delta")
        self._now += ms
        return self._now

    def advance_vsyncs(self, n: int) -> int:
        """Move the clock forward by exactly ``n * vsync_ms``."""
        if n < 0:
            raise ValueError("advance_vsyncs requires a non-negative count")
        self._now += n * self._vsync
        return self._now


def step_frames(
    clock: VirtualClock, n: int, on_frame: Callable[[int], None]
) -> list[int]:
    """Advance ``clock`` by ``n`` vsyncs, invoking ``on_frame`` per frame.

    Returns the visited zero-based frame indices ``[0..n-1]``.
    """
    if n < 0:
        raise ValueError("step_frames requires a non-negative count")
    visited = []
    for frame in range(n):
        clock.advance_vsyncs(1)
        on_frame(frame)
        visited.append(frame)
    return visited
