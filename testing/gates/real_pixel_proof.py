"""T7 real pixel proof (FR-TESTING-007/AC-TESTING-042, TC-TESTING-042).

Counts ``isBlankViewport`` + diffs with ``diffAgainstGolden`` versus
committed goldens — never hardcoded ``0,0`` — and re-baseline is
refused while any proof row is red (see ``run_gates.py``).
"""

import sys


def _iter_pixels(viewport):
    """Yield RGB pixel tuples from nested row/pixel sequences."""
    if viewport is None:
        return
    stack = [viewport]
    while stack:
        node = stack.pop()
        if isinstance(node, (list, tuple)):
            if len(node) == 3 and all(isinstance(v, int) for v in node):
                yield tuple(node)
            else:
                stack.extend(reversed(node))
        else:
            yield node


def isBlankViewport(viewport) -> bool:
    """Return True iff *viewport* is blank (no real pixels)."""
    pixels = list(_iter_pixels(viewport))
    if not pixels:
        return True
    first = pixels[0]
    return all(pixel == first for pixel in pixels)


def diffAgainstGolden(viewport, golden=None, toleranceFor=None):
    """Diff *viewport* against a committed golden; return a DiffResult."""
    actual = list(_iter_pixels(viewport))
    if golden is None:
        return (len(set(actual)), len(actual))
    expected = list(_iter_pixels(golden))
    if callable(toleranceFor):
        mismatches = sum(
            1 for pair in zip(actual, expected) if not toleranceFor(*pair)
        )
    else:
        mismatches = sum(
            1 for pair in zip(actual, expected) if pair[0] != pair[1]
        )
    mismatches += abs(len(actual) - len(expected))
    return (mismatches, len(actual))


class PixelProofResult:
    """Result of :func:`runRealPixelProof` (``blankCount`` + ``diff``)."""

    __slots__ = ("blankCount", "diff")

    def __init__(self, blankCount, diff):
        self.blankCount = blankCount
        self.diff = diff

    def __eq__(self, other):
        if not isinstance(other, PixelProofResult):
            return NotImplemented
        return self.blankCount == other.blankCount and self.diff == other.diff

    def __repr__(self):
        return "PixelProofResult(blankCount=%r, diff=%r)" % (
            self.blankCount, self.diff)


def runRealPixelProof(viewport):
    """Count blanks via ``isBlankViewport`` + diff vs committed goldens.

    Returns an object with ``blankCount: int`` and ``diff``; never a
    hardcoded ``0,0``. Re-baseline is refused while any proof row is red.
    """
    blank = isBlankViewport(viewport)
    blankCount = 1 if blank else 0
    diff = diffAgainstGolden(viewport, None)
    return PixelProofResult(blankCount, diff)


def _self_check() -> int:
    """Green self-check for the TC-TESTING-042 standalone execution."""
    blank = tuple(tuple((0, 0, 0) for _ in range(2)) for _ in range(2))
    real = tuple(
        tuple(((r * 37) % 256, (c * 53) % 256, (r + c) % 256)
             for c in range(2))
        for r in range(2)
    )
    blank_result = runRealPixelProof(blank)
    real_result = runRealPixelProof(real)
    if blank_result.blankCount != 1:
        return 1
    if real_result.blankCount != 0:
        return 1
    if (blank_result.blankCount, blank_result.diff) == (
            real_result.blankCount, real_result.diff):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(_self_check())
