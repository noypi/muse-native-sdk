"""Frame-budget shed decision (NFR-PERFORMANCE-011, AC-PERFORMANCE-026).

Hermetic: costs are explicit inputs; no clock, device, or display.
Deterministic: shedding follows input order, skipping mandatory items.
Fail closed: negative budgets or costs raise ``ValueError``.
"""

from dataclasses import dataclass


@dataclass
class ShedEvent:
    subsystem: str
    reason: str
    at_ms: float


class ShedLog:
    """Telemetry log recording one event per shed item."""

    def __init__(self) -> None:
        self._events: list[ShedEvent] = []

    def record(self, subsystem: str, reason: str, at_ms: float) -> ShedEvent:
        if not subsystem:
            raise ValueError("empty subsystem for shed event")
        if not reason:
            raise ValueError("empty reason for shed event")
        event = ShedEvent(subsystem=subsystem, reason=reason, at_ms=at_ms)
        self._events.append(event)
        return event

    def events(self) -> list[ShedEvent]:
        return list(self._events)


@dataclass
class WorkItem:
    name: str
    subsystem: str
    sheddable: bool
    cost_ms: float


class FrameGuard:
    """Shed only sheddable work so the kept frame fits ``budget_ms``."""

    def __init__(self, budget_ms: float, shed_log: ShedLog | None = None) -> None:
        if budget_ms < 0:
            raise ValueError(f"negative frame budget: {budget_ms!r}")
        self.budget_ms = budget_ms
        self.shed_log = shed_log

    def decide(
        self, items: list[WorkItem]
    ) -> tuple[list[WorkItem], list[WorkItem]]:
        """Split ``items`` into ``(kept, shed)``.

        Sheds sheddable items in input order, skipping mandatory items,
        until the projected kept cost fits the budget. Mandatory items
        are never in ``shed``, even when mandatory cost alone exceeds
        the budget.
        """
        for item in items:
            if item.cost_ms < 0:
                raise ValueError(
                    f"negative cost for work item {item.name!r}: "
                    f"{item.cost_ms!r}"
                )
        kept = list(items)
        shed: list[WorkItem] = []
        projected = sum(item.cost_ms for item in kept)
        excess_ms = projected - self.budget_ms
        if projected <= self.budget_ms:
            return kept, shed
        for item in items:
            if projected <= self.budget_ms:
                break
            if not item.sheddable:
                continue
            kept.remove(item)
            shed.append(item)
            projected -= item.cost_ms
        if self.shed_log is not None:
            for item in shed:
                self.shed_log.record(
                    subsystem=item.subsystem,
                    reason=f"over-budget:{excess_ms}ms",
                    at_ms=0.0,
                )
        return kept, shed
