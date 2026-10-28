"""Thermal headroom -> quality-tier request signal (perf side).

Pure function: maps remaining thermal headroom to the quality tier the
perf harness requests from graphics. Tier switching itself stays in
GFX-010 (out of scope); this module only emits the request signal and
never drops frames itself.

Tier ladder per GFX-010 Notes, ordered most to least degraded:
low < medium < high < ultra. Each step-down sheds non-essential work
so sustained p99 stays within the 8 ms complex-frame budget.
"""

from dataclasses import dataclass

# Headroom cut points (ms) mirroring the perf->graphics contract.
# Falling headroom steps tiers down monotonically.
_ULTRA_MIN_MS = 5.0
_HIGH_MIN_MS = 2.0
_MEDIUM_MIN_MS = 0.5


@dataclass(frozen=True)
class Tier:
    name: str
    degraded: bool


def tier_for_headroom(headroom_ms: float) -> Tier:
    """Return the requested quality tier for remaining headroom.

    Pure: no I/O, no thermal-control read, no frame drop. Only
    ``ultra`` is non-degraded; every step down is degraded.
    """
    if headroom_ms >= _ULTRA_MIN_MS:
        return Tier(name="ultra", degraded=False)
    if headroom_ms >= _HIGH_MIN_MS:
        return Tier(name="high", degraded=True)
    if headroom_ms >= _MEDIUM_MIN_MS:
        return Tier(name="medium", degraded=True)
    return Tier(name="low", degraded=True)
