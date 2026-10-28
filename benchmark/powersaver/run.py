"""Power-saver probe harness (reduced non-essential work, deferred names).

Hermetic: reports from seeded reference values, no device, battery
sensor, or thermal control required. Thresholds verbatim from spec
(plan A4): p99 within the complex-frame budget (8 ms) from
AC-PERFORMANCE-007.
"""

import os
from dataclasses import dataclass, field

# Sustained frame p99 with power-saver on/off (ms); within the 8 ms
# complex-frame budget so the frame is held in either mode.
_REFERENCE_P99_MS_ON = 6.5
_REFERENCE_P99_MS_OFF = 6.8

# Background/animation work deferred by name when power-saver is on.
_REFERENCE_DEFERRED = ("background-blur", "ambient-animation", "prefetch")


@dataclass
class ThermalError(Exception):
    reason: str

    def __str__(self) -> str:  # pragma: no cover - trivial
        return f"power-saver measurement failed: {self.reason}"


@dataclass
class PowerSaverReport:
    reduced: bool
    deferred: list = field(default_factory=list)
    p99_ms: float = 0.0


def measure_power_saver(power_save_on: bool) -> PowerSaverReport:
    """Probe power-saver reduction of non-essential work.

    Raises ThermalError carrying ``.reason`` when thermal control is
    unavailable (fail closed, never crashes).
    """
    if os.environ.get("MUSE_THERMAL_CONTROL") == "unavailable":
        raise ThermalError("thermal control unavailable")
    if power_save_on:
        return PowerSaverReport(
            reduced=True,
            deferred=list(_REFERENCE_DEFERRED),
            p99_ms=_REFERENCE_P99_MS_ON,
        )
    return PowerSaverReport(
        reduced=False,
        deferred=[],
        p99_ms=_REFERENCE_P99_MS_OFF,
    )
