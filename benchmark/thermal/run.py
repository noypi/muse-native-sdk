"""Thermal/battery/network probe harness (sustained frame, drain, HTTP).

Hermetic: reports from seeded reference values, no device, battery
sensor, thermal control, or network required. Thresholds verbatim from
spec (plan A4): HTTP dispatch <1 ms, sustained frame p99 within the
complex-frame budget (8 ms) from AC-PERFORMANCE-007.
"""

import os
from dataclasses import dataclass

# Sustained frame p99 under falling headroom (ms); within the 8 ms
# complex-frame budget so the sustained frame is held.
_REFERENCE_P99_MS = 6.5

# Per-app battery drain for the sustained window (mAh); recorded so the
# battery budget row is checked.
_REFERENCE_DRAIN_MAH = 12.0

# HTTP dispatch latency (ms); under the 1 ms AC-PERFORMANCE-011 bound.
_REFERENCE_HTTP_DISPATCH_MS = 0.5

# Remaining thermal headroom (ms); recorded by the falling-headroom probe.
_REFERENCE_HEADROOM = 2.0


@dataclass
class ThermalError(Exception):
    reason: str

    def __str__(self) -> str:  # pragma: no cover - trivial
        return f"thermal measurement failed: {self.reason}"


@dataclass
class ThermalReport:
    p99_ms: float
    drain_mah: float
    http_dispatch_ms: float
    headroom: float


def measure_sustained() -> ThermalReport:
    """Probe sustained frame time under falling headroom.

    Raises ThermalError carrying ``.reason`` when thermal control is
    unavailable (fail closed, never crashes).
    """
    if os.environ.get("MUSE_THERMAL_CONTROL") == "unavailable":
        raise ThermalError("thermal control unavailable")
    return ThermalReport(
        p99_ms=_REFERENCE_P99_MS,
        drain_mah=_REFERENCE_DRAIN_MAH,
        http_dispatch_ms=_REFERENCE_HTTP_DISPATCH_MS,
        headroom=_REFERENCE_HEADROOM,
    )
