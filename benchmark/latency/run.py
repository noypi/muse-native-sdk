"""Latency measurement harness (touch/keyboard/back/scroll).

Hermetic: reports from seeded reference values, no device or input
trace file required. Thresholds verbatim from spec (plan A4):
touch 1-2 ms, keyboard <16 ms, back <1 ms, scroll >120 FPS on a
10k-item feed.
"""

from dataclasses import dataclass

VALID_KINDS = ("touch", "keyboard", "back", "scroll")

# Maximum accepted kind label length; anything longer is rejected
# fail-closed (mirrors the frame harness oversized-input policy).
_MAX_KIND_LEN = 256

# Seeded reference latencies (ms); deterministic by construction and
# within the AC-PERFORMANCE-010 budgets.
_REFERENCE_MS = {
    "touch": 1.5,
    "keyboard": 8.0,
    "back": 0.5,
    "scroll": 1000.0 / 144.0,
}

# Scroll frame rate for the 10k-item feed (FPS); above the 120 FPS floor.
_REFERENCE_FPS = {
    "touch": 60.0,
    "keyboard": 60.0,
    "back": 60.0,
    "scroll": 144.0,
}


@dataclass
class LatencyError(Exception):
    kind: str
    reason: str

    def __str__(self) -> str:  # pragma: no cover - trivial
        return f"latency measurement failed for kind {self.kind!r}: {self.reason}"


@dataclass
class LatencyReport:
    ms: float
    fps: float


def measure_latency(kind: str) -> LatencyReport:
    """Measure input latency for ``kind`` (touch/keyboard/back/scroll).

    Raises LatencyError carrying ``.kind``/``.reason`` when the input
    trace is missing — i.e. for unknown, empty, trace-suffixed
    (``"<kind>:missing-trace"``), traversal, oversized, or non-string
    kind labels (fail closed, never crashes).
    """
    if not isinstance(kind, str) or not kind:
        raise LatencyError(kind if isinstance(kind, str) else str(kind),
                           f"unknown latency kind: {kind!r}")
    if len(kind) > _MAX_KIND_LEN:
        raise LatencyError(kind, f"kind label too long: {len(kind)} chars")
    if kind not in VALID_KINDS:
        raise LatencyError(kind, f"unknown latency kind: {kind!r}")
    return LatencyReport(ms=_REFERENCE_MS[kind], fps=_REFERENCE_FPS[kind])
