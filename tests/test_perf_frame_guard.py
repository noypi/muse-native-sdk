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


class ShedLogContractTest(unittest.TestCase):
    """Batch 2 (T2) — every shed decision is recorded with subsystem+reason.

    Covers NFR-PERFORMANCE-011 AC-PERFORMANCE-027 (TC-PERFORMANCE-027,
    missing) per specs-performance.md v3.2: every shed is a telemetry
    event carrying subsystem and reason.

    Public boundary under test (plan Checklist T2, verbatim)::

        ShedEvent(subsystem: str, reason: str, at_ms: float); ShedLog()
        with record(subsystem: str, reason: str, at_ms: float) -> ShedEvent
        and events() -> list[ShedEvent]; raises ValueError on empty
        subsystem or reason. FrameGuard takes an optional ShedLog and
        records one event per shed item with reason naming the budget
        pressure (e.g. over-budget:<excess_ms>ms).

    Hermetic: stdlib unittest only, explicit at_ms constants, no clock.
    Expected state before implementation: RED — ShedLog/ShedEvent
    missing (ModuleNotFoundError/ImportError/AttributeError).
    """

    def import_shed(self):
        from engine.perf.guard.guard import ShedEvent, ShedLog
        return ShedEvent, ShedLog

    def test_record_returns_event_with_fields(self):
        _, ShedLog = self.import_shed()
        log = ShedLog()
        event = log.record(subsystem="fx", reason="over-budget:5.0ms", at_ms=1.0)
        self.assertEqual(event.subsystem, "fx")
        self.assertEqual(event.reason, "over-budget:5.0ms")
        self.assertEqual(event.at_ms, 1.0)

    def test_events_returns_recorded_events(self):
        _, ShedLog = self.import_shed()
        log = ShedLog()
        log.record(subsystem="fx", reason="over-budget:5.0ms", at_ms=1.0)
        log.record(subsystem="ui", reason="over-budget:2.0ms", at_ms=2.0)
        events = log.events()
        self.assertEqual(len(events), 2)

    def test_record_empty_subsystem_raises_value_error(self):
        _, ShedLog = self.import_shed()
        log = ShedLog()
        with self.assertRaises(ValueError):
            log.record(subsystem="", reason="over-budget:1.0ms", at_ms=0.0)

    def test_record_empty_reason_raises_value_error(self):
        _, ShedLog = self.import_shed()
        log = ShedLog()
        with self.assertRaises(ValueError):
            log.record(subsystem="fx", reason="", at_ms=0.0)


class FrameGuardShedLogTest(unittest.TestCase):
    """Batch 2 (T2) — FrameGuard(log) records one event per shed item."""

    def test_shed_n_items_records_exactly_n_events(self):
        FrameGuard, WorkItem = import_guard()
        from engine.perf.guard.guard import ShedLog
        log = ShedLog()
        guard = FrameGuard(8.0, log)
        items = [
            WorkItem(name="m1", subsystem="ui", sheddable=False, cost_ms=2.0),
            WorkItem(name="m2", subsystem="layout", sheddable=False, cost_ms=2.0),
            WorkItem(name="s1", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s2", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s3", subsystem="fx", sheddable=True, cost_ms=3.0),
        ]
        _, shed = guard.decide(items)
        events = log.events()
        self.assertGreater(len(shed), 0)
        self.assertEqual(len(events), len(shed))

    def test_events_have_non_empty_subsystem_and_reason(self):
        FrameGuard, WorkItem = import_guard()
        from engine.perf.guard.guard import ShedLog
        log = ShedLog()
        guard = FrameGuard(8.0, log)
        items = [
            WorkItem(name="m1", subsystem="ui", sheddable=False, cost_ms=2.0),
            WorkItem(name="s1", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s2", subsystem="fx", sheddable=True, cost_ms=4.0),
        ]
        _, shed = guard.decide(items)
        self.assertGreater(len(shed), 0)
        for event in log.events():
            self.assertTrue(event.subsystem)
            self.assertTrue(event.reason)

    def test_every_shed_item_has_matching_event(self):
        FrameGuard, WorkItem = import_guard()
        from engine.perf.guard.guard import ShedLog
        log = ShedLog()
        guard = FrameGuard(8.0, log)
        items = [
            WorkItem(name="m1", subsystem="ui", sheddable=False, cost_ms=2.0),
            WorkItem(name="m2", subsystem="layout", sheddable=False, cost_ms=2.0),
            WorkItem(name="s1", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s2", subsystem="fx", sheddable=True, cost_ms=3.0),
            WorkItem(name="s3", subsystem="fx", sheddable=True, cost_ms=3.0),
        ]
        _, shed = guard.decide(items)
        subsystems = [event.subsystem for event in log.events()]
        for item in shed:
            self.assertIn(item.subsystem, subsystems)

    def test_shed_event_reason_names_budget_pressure(self):
        # ASSUMPTION: reason naming the budget pressure means the literal
        # prefix "over-budget:" per the plan example
        # (e.g. over-budget:<excess_ms>ms).
        FrameGuard, WorkItem = import_guard()
        from engine.perf.guard.guard import ShedLog
        log = ShedLog()
        guard = FrameGuard(8.0, log)
        items = [
            WorkItem(name="m1", subsystem="ui", sheddable=False, cost_ms=2.0),
            WorkItem(name="s1", subsystem="fx", sheddable=True, cost_ms=10.0),
        ]
        _, shed = guard.decide(items)
        self.assertGreater(len(shed), 0)
        for event in log.events():
            self.assertIn("over-budget:", event.reason)


class ExpensiveFrameHarnessTest(unittest.TestCase):
    """Batch 3 (T3) — expensive-frame harness proving the frame stays in budget.

    Covers NFR-PERFORMANCE-011 AC-PERFORMANCE-028 (TC-PERFORMANCE-028,
    missing) and NFR-PERFORMANCE-005 AC-PERFORMANCE-005 (TC-PERFORMANCE-005,
    missing) per specs-performance.md v3.2: under injected expensive load
    the guard sheds only sheddable work and holds the visible frame within
    budget.

    Public boundary under test (plan Checklist T3, verbatim)::

        run_expensive_frame(sheddable_cost_ms: float, mandatory_cost_ms: float, budget_ms: float) -> GuardReport(frame_ms: float, within_budget: bool, shed_count: int, mandatory_lost: bool); raises FrameGuardError(frame, reason) carrying .frame/.reason on negative inputs or non-positive budget (fail closed, mirroring FrameError in benchmark/frame/run.py).

    Hermetic: stdlib unittest only, seeded constants, no clock, no device,
    no display. Expected state before implementation: RED —
    ``benchmark/frame_guard/run.py`` does not exist, so every test fails
    (ModuleNotFoundError).
    """

    # Seeded constants (hermetic, no device).
    SHEDDABLE_COST_MS = 12.0
    MANDATORY_COST_MS = 3.0
    BUDGET_MS = 8.0

    def import_harness(self):
        """Import the harness through its public boundary."""
        from benchmark.frame_guard.run import FrameGuardError, run_expensive_frame
        return FrameGuardError, run_expensive_frame

    def test_expensive_frame_stays_within_budget(self):
        _, run_expensive_frame = self.import_harness()
        report = run_expensive_frame(
            self.SHEDDABLE_COST_MS, self.MANDATORY_COST_MS, self.BUDGET_MS
        )
        self.assertTrue(report.within_budget)
        self.assertLessEqual(report.frame_ms, self.BUDGET_MS)

    def test_expensive_frame_keeps_mandatory(self):
        _, run_expensive_frame = self.import_harness()
        report = run_expensive_frame(
            self.SHEDDABLE_COST_MS, self.MANDATORY_COST_MS, self.BUDGET_MS
        )
        self.assertFalse(report.mandatory_lost)

    def test_expensive_frame_sheds_at_least_one(self):
        _, run_expensive_frame = self.import_harness()
        report = run_expensive_frame(
            self.SHEDDABLE_COST_MS, self.MANDATORY_COST_MS, self.BUDGET_MS
        )
        self.assertGreaterEqual(report.shed_count, 1)

    def test_total_under_budget_sheds_nothing(self):
        # ASSUMPTION: total cost (sheddable + mandatory) within budget
        # keeps everything; seeded constant 2.0 + 3.0 = 5.0 <= 8.0.
        _, run_expensive_frame = self.import_harness()
        report = run_expensive_frame(2.0, 3.0, self.BUDGET_MS)
        self.assertEqual(report.shed_count, 0)
        self.assertTrue(report.within_budget)
        self.assertFalse(report.mandatory_lost)


class FrameGuardHarnessValidationTest(unittest.TestCase):
    """Batch 3 (T3) — fail-closed validation mirroring FrameError."""

    def import_harness(self):
        from benchmark.frame_guard.run import FrameGuardError, run_expensive_frame
        return FrameGuardError, run_expensive_frame

    def test_negative_sheddable_raises_frame_guard_error(self):
        FrameGuardError, run_expensive_frame = self.import_harness()
        with self.assertRaises(FrameGuardError):
            run_expensive_frame(-1.0, 3.0, 8.0)

    def test_negative_mandatory_raises_frame_guard_error(self):
        FrameGuardError, run_expensive_frame = self.import_harness()
        with self.assertRaises(FrameGuardError):
            run_expensive_frame(12.0, -1.0, 8.0)

    def test_non_positive_budget_raises_frame_guard_error(self):
        FrameGuardError, run_expensive_frame = self.import_harness()
        with self.assertRaises(FrameGuardError):
            run_expensive_frame(12.0, 3.0, 0.0)
        with self.assertRaises(FrameGuardError):
            run_expensive_frame(12.0, 3.0, -8.0)

    def test_error_carries_frame_and_reason(self):
        _, run_expensive_frame = self.import_harness()
        from benchmark.frame_guard.run import FrameGuardError
        try:
            run_expensive_frame(-1.0, 3.0, 8.0)
        except FrameGuardError as exc:
            self.assertTrue(exc.reason)
            self.assertIsNotNone(exc.frame)
        else:
            self.fail("FrameGuardError not raised")


if __name__ == "__main__":
    unittest.main()
