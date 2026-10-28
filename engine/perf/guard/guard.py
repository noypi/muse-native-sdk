"""Frame-budget shed decision (NFR-PERFORMANCE-011, AC-PERFORMANCE-026).

Hermetic: costs are explicit inputs; no clock, device, or display.
Deterministic: shedding follows input order, skipping mandatory items.
Fail closed: negative budgets or costs raise ``ValueError``.
"""

from dataclasses import dataclass


@dataclass
class WorkItem:
    name: str
    subsystem: str
    sheddable: bool
    cost_ms: float


class FrameGuard:
    """Shed only sheddable work so the kept frame fits ``budget_ms``."""

    def __init__(self, budget_ms: float) -> None:
        if budget_ms < 0:
            raise ValueError(f"negative frame budget: {budget_ms!r}")
        self.budget_ms = budget_ms

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
        return kept, shed
