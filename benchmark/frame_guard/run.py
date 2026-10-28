"""Expensive-frame guard harness (NFR-PERFORMANCE-011, NFR-PERFORMANCE-005).

Hermetic: costs and budget are explicit seeded inputs; no device,
display, clock, or trace file required. Builds one mandatory plus one
sheddable ``WorkItem`` (subsystem ``perf-guard``), runs the T1/T2
``FrameGuard.decide`` with a ``ShedLog``, and reports the kept cost as
the visible frame time.
"""

from dataclasses import dataclass

from engine.perf.guard.guard import FrameGuard, ShedLog, WorkItem

_SUBSYSTEM = "perf-guard"


@dataclass
class FrameGuardError(Exception):
    frame: str
    reason: str

    def __str__(self) -> str:  # pragma: no cover - trivial
        return f"frame guard failed for frame {self.frame!r}: {self.reason}"


@dataclass
class GuardReport:
    frame_ms: float
    within_budget: bool
    shed_count: int
    mandatory_lost: bool


def run_expensive_frame(
    sheddable_cost_ms: float, mandatory_cost_ms: float, budget_ms: float
) -> GuardReport:
    """Run one guarded frame under injected expensive load.

    Raises FrameGuardError carrying ``.frame``/``.reason`` on negative
    inputs or non-positive budget (fail closed).
    """
    frame = (
        f"sheddable_cost_ms={sheddable_cost_ms!r},"
        f"mandatory_cost_ms={mandatory_cost_ms!r},"
        f"budget_ms={budget_ms!r}"
    )
    if sheddable_cost_ms < 0:
        raise FrameGuardError(frame, f"negative sheddable cost: {sheddable_cost_ms!r}")
    if mandatory_cost_ms < 0:
        raise FrameGuardError(frame, f"negative mandatory cost: {mandatory_cost_ms!r}")
    if budget_ms <= 0:
        raise FrameGuardError(frame, f"non-positive frame budget: {budget_ms!r}")
    items = [
        WorkItem(
            name="mandatory",
            subsystem=_SUBSYSTEM,
            sheddable=False,
            cost_ms=mandatory_cost_ms,
        ),
        WorkItem(
            name="sheddable",
            subsystem=_SUBSYSTEM,
            sheddable=True,
            cost_ms=sheddable_cost_ms,
        ),
    ]
    guard = FrameGuard(budget_ms, ShedLog())
    kept, shed = guard.decide(items)
    kept_names = {item.name for item in kept}
    frame_ms = sum(item.cost_ms for item in kept)
    return GuardReport(
        frame_ms=frame_ms,
        within_budget=frame_ms <= budget_ms,
        shed_count=len(shed),
        mandatory_lost="mandatory" not in kept_names,
    )
