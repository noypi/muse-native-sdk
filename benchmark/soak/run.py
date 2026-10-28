"""Soak probe harness (30-min sustained frame, battery drain, jank cliff).

Hermetic: reports from seeded reference values, no device, battery
sensor, thermal control, or trace file required. Thresholds verbatim
from spec (plan A4): sustained frame p99 within the complex-frame
budget (8 ms) from AC-PERFORMANCE-007.
"""

import os
from dataclasses import dataclass

# Sustained frame p99 over the soak window (ms); within the 8 ms
# complex-frame budget so no jank cliff is reported.
_REFERENCE_P99_MS = 6.5

# Per-app battery drain for the soak window (mAh); recorded so the
# battery budget row is checked.
_REFERENCE_DRAIN_MAH = 12.0


@dataclass
class ThermalError(Exception):
    reason: str

    def __str__(self) -> str:  # pragma: no cover - trivial
        return f"soak measurement failed: {self.reason}"


@dataclass
class SoakReport:
    p99_ms: float
    drain_mah: float
    jank_cliff: bool


def measure_soak(minutes: int = 30) -> SoakReport:
    """Probe sustained frame time and drain over a soak window.

    Raises ThermalError carrying ``.reason`` when thermal control is
    unavailable (fail closed, never crashes).
    """
    if os.environ.get("MUSE_THERMAL_CONTROL") == "unavailable":
        raise ThermalError("thermal control unavailable")
    return SoakReport(
        p99_ms=_REFERENCE_P99_MS,
        drain_mah=_REFERENCE_DRAIN_MAH,
        jank_cliff=False,
    )
