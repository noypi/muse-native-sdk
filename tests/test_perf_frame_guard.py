"""Batch 1 (T1) — frame-budget shed decision (sheddable/mandatory classification).

Covers NFR-PERFORMANCE-011 AC-PERFORMANCE-026 (TC-PERFORMANCE-026, missing)
per specs-performance.md v3.2: optional work is classified as sheddable
or mandatory; the guard only cancels or defers work classified sheddable.

Public boundary under test (plan Checklist T1)::

    WorkItem(name: str, subsystem: str, sheddable: bool, cost_ms: float)
    FrameGuard(budget_ms: float) with
        decide(items: list[WorkItem]) -> tuple[list[WorkItem], list[WorkItem]]
        returning (kept, shed); raises ValueError on negative budget_ms
        or negative cost_ms. Mandatory items are never in shed.

Hermetic: stdlib unittest only, no network/secrets/services, no clock,
no device. Deterministic: no randomness.

Expected state before implementation: RED —
``engine/perf/guard/guard.py`` does not exist, so every test fails
(ModuleNotFoundError).
"""

import unittest


def import_guard():
    """Import the guard through its public boundary."""
    from engine.perf.guard.guard import FrameGuard, WorkItem
    return FrameGuard, WorkItem


def kept_cost(kept):
    return sum(item.cost_ms for item in kept)


class ShedDecisionTest(unittest.TestCase):
    def test_over_budget_keeps_all_mandatory(self):
        FrameGuard, WorkItem = import_guard()
        guard = FrameGuard(8.0)
        items = [
            WorkItem(name="m1", subsystem="ui", sheddable=False, cost_ms=2.0),
            WorkItem(name="m2", subsystem="layout", sheddable=False, cost_ms=2.0),
            WorkItem(name="s1", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s2", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s3", subsystem="fx", sheddable=True, cost_ms=3.0),
        ]
        kept, shed = guard.decide(items)
        kept_names = {item.name for item in kept}
        self.assertIn("m1", kept_names)
        self.assertIn("m2", kept_names)

    def test_over_budget_kept_cost_within_budget(self):
        FrameGuard, WorkItem = import_guard()
        guard = FrameGuard(8.0)
        items = [
            WorkItem(name="m1", subsystem="ui", sheddable=False, cost_ms=2.0),
            WorkItem(name="m2", subsystem="layout", sheddable=False, cost_ms=2.0),
            WorkItem(name="s1", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s2", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s3", subsystem="fx", sheddable=True, cost_ms=3.0),
        ]
        kept, _ = guard.decide(items)
        self.assertLessEqual(kept_cost(kept), 8.0)

    def test_only_sheddable_items_are_shed(self):
        FrameGuard, WorkItem = import_guard()
        guard = FrameGuard(8.0)
        items = [
            WorkItem(name="m1", subsystem="ui", sheddable=False, cost_ms=2.0),
            WorkItem(name="m2", subsystem="layout", sheddable=False, cost_ms=2.0),
            WorkItem(name="s1", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s2", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s3", subsystem="fx", sheddable=True, cost_ms=3.0),
        ]
        kept, shed = guard.decide(items)
        self.assertGreater(len(shed), 0)
        for item in shed:
            self.assertTrue(item.sheddable)
        for item in kept:
            if not item.sheddable:
                continue

    def test_mandatory_never_shed_even_when_mandatory_exceeds_budget(self):
        # ASSUMPTION: when mandatory cost alone exceeds the budget, the
        # guard keeps every mandatory item (mandatory never in shed) even
        # though kept cost then exceeds budget; only sheddable is shed.
        FrameGuard, WorkItem = import_guard()
        guard = FrameGuard(2.0)
        items = [
            WorkItem(name="m1", subsystem="ui", sheddable=False, cost_ms=2.0),
            WorkItem(name="m2", subsystem="layout", sheddable=False, cost_ms=2.0),
            WorkItem(name="s1", subsystem="fx", sheddable=True, cost_ms=1.0),
        ]
        kept, shed = guard.decide(items)
        kept_names = {item.name for item in kept}
        self.assertIn("m1", kept_names)
        self.assertIn("m2", kept_names)
        for item in shed:
            self.assertTrue(item.sheddable)

    def test_under_budget_nothing_shed(self):
        # ASSUMPTION: total cost within budget keeps everything.
        FrameGuard, WorkItem = import_guard()
        guard = FrameGuard(20.0)
        items = [
            WorkItem(name="m1", subsystem="ui", sheddable=False, cost_ms=2.0),
            WorkItem(name="s1", subsystem="fx", sheddable=True, cost_ms=3.0),
        ]
        kept, shed = guard.decide(items)
        self.assertEqual(len(shed), 0)
        self.assertEqual(len(kept), 2)


class GuardValidationTest(unittest.TestCase):
    def test_negative_budget_raises_value_error(self):
        FrameGuard, _ = import_guard()
        with self.assertRaises(ValueError):
            FrameGuard(-1.0)

    def test_negative_cost_raises_value_error(self):
        FrameGuard, WorkItem = import_guard()
        guard = FrameGuard(8.0)
        items = [
            WorkItem(name="bad", subsystem="fx", sheddable=True, cost_ms=-1.0),
        ]
        with self.assertRaises(ValueError):
            guard.decide(items)


if __name__ == "__main__":
    unittest.main()
