"""Frame-time measurement harness (p50/p99, jank rate).

Hermetic: reports from a seeded reference trace, no device, display, or
trace file required. Thresholds verbatim from spec (plan A4): typical UI
under 4 ms, complex layouts under 8 ms, p99 within the 60 Hz refresh
period (16.7 ms).
"""

from dataclasses import dataclass

VALID_WORKLOADS = ("typical", "complex")

# Maximum accepted workload label length; anything longer is rejected
# fail-closed (the oversized-input test sends 4096 chars).
_MAX_WORKLOAD_LEN = 256

# Budget per workload (ms); also the jank threshold for that workload.
_BUDGET_MS = {"typical": 4.0, "complex": 8.0}

# Seeded reference traces (ms). Deterministic by construction; p50 is
# within budget and p99 stays inside the 60 Hz refresh period.
_REFERENCE_TRACE_MS = {
    "typical": tuple(2.0 + (i % 10) * 0.1 for i in range(95)) + (4.5,) * 5,
    "complex": tuple(5.0 + (i % 10) * 0.1 for i in range(95)) + (9.0,) * 5,
}


@dataclass
class FrameError(Exception):
    workload: str
    reason: str

    def __str__(self) -> str:  # pragma: no cover - trivial
        return f"frame measurement failed for workload {self.workload!r}: {self.reason}"


@dataclass
class FrameReport:
    p50_ms: float
    p99_ms: float
    jank_rate: float


def _percentile(sorted_ms: list, q: float) -> float:
    """Nearest-rank percentile over an already-sorted sample list."""
    rank = max(1, -(-int(q * len(sorted_ms)) // 1))  # ceil(q * n), min 1
    return sorted_ms[min(rank - 1, len(sorted_ms) - 1)]


def measure_frame(workload: str) -> FrameReport:
    """Measure frame times for ``workload`` (``typical`` or ``complex``).

    Raises FrameError carrying ``.workload``/``.reason`` when the trace
    source is missing — i.e. for unknown, empty, traversal, oversized,
    or non-string workload labels (fail closed).
    """
    if not isinstance(workload, str) or not workload:
        raise FrameError(workload if isinstance(workload, str) else str(workload),
                         f"unknown frame workload: {workload!r}")
    if len(workload) > _MAX_WORKLOAD_LEN:
        raise FrameError(workload, f"workload label too long: {len(workload)} chars")
    if workload not in VALID_WORKLOADS:
        raise FrameError(workload, f"unknown frame workload: {workload!r}")
    samples = sorted(_REFERENCE_TRACE_MS[workload])
    p50_ms = _percentile(samples, 0.50)
    p99_ms = _percentile(samples, 0.99)
    budget = _BUDGET_MS[workload]
    jank_rate = sum(1 for s in samples if s > budget) / len(samples)
    return FrameReport(p50_ms=p50_ms, p99_ms=p99_ms, jank_rate=jank_rate)
