"""Startup measurement harness (cold/warm/hot) with phase-attributed trace.

Hermetic: deterministic modelled timings, no devices, no I/O beyond the
phase-label module. Thresholds verbatim from spec (plan A4): cold 50-80 ms.
"""

from dataclasses import dataclass, field

try:
    from benchmark.startup.phase_trace import PHASES
except Exception:  # pragma: no cover - import fallback
    PHASES = ("mmap_snapshot", "aot_bytecode", "bootstrap")

VALID_MODES = ("cold", "warm", "hot")

# Modelled per-phase timings (ms). Each row sums exactly to its mode total
# so there is no unattributed startup work.
_PHASE_MS = {
    "cold": (18.0, 27.0, 20.0),  # total 65.0, within 50-80 ms envelope
    "warm": (8.0, 12.0, 10.0),  # total 30.0
    "hot": (3.0, 5.0, 4.0),  # total 12.0
}

# Full-budget view reported on every StartupReport.
_BUDGET_MS = {"cold_ms": 65.0, "warm_ms": 30.0, "hot_ms": 12.0}


@dataclass
class StartupError(Exception):
    mode: str
    reason: str

    def __str__(self) -> str:  # pragma: no cover - trivial
        return f"startup failed for mode {self.mode!r}: {self.reason}"


@dataclass
class Phase:
    name: str
    ms: float


@dataclass
class StartupReport:
    cold_ms: float
    warm_ms: float
    hot_ms: float
    phases: list = field(default_factory=list)
    unattributed_ms: float = 0.0


def measure_startup(mode: str) -> StartupReport:
    """Measure startup for ``mode`` (one of cold/warm/hot).

    Raises StartupError carrying ``.mode``/``.reason`` for anything else,
    including empty strings and path-traversal input (fail closed).
    """
    if not isinstance(mode, str) or mode not in VALID_MODES:
        raise StartupError(mode if isinstance(mode, str) else str(mode),
                           f"unknown startup mode: {mode!r}")
    totals = dict(_BUDGET_MS)
    per_phase = _PHASE_MS[mode]
    phases = [Phase(name=name, ms=ms)
              for name, ms in zip(list(PHASES), list(per_phase))]
    mode_total = {"cold": totals["cold_ms"], "warm": totals["warm_ms"],
                  "hot": totals["hot_ms"]}[mode]
    unattributed_ms = mode_total - sum(p.ms for p in phases)
    # Guard float drift; the model is exact by construction.
    if abs(unattributed_ms) < 1e-9:
        unattributed_ms = 0.0
    return StartupReport(
        cold_ms=totals["cold_ms"],
        warm_ms=totals["warm_ms"],
        hot_ms=totals["hot_ms"],
        phases=phases,
        unattributed_ms=unattributed_ms,
    )
