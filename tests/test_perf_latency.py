"""Batch 3 (T6) — latency measurement harness.

Covers NFR-PERFORMANCE-006 (AC-PERFORMANCE-010): touch 1-2 ms,
keyboard <16 ms, back <1 ms, scroll >120 FPS on 10k-item feed.

Public boundary under test (plan Checklist T6)::

    measure_latency(kind: str) -> LatencyReport(ms: float, fps: float)
    LatencyError(kind: str, reason: str)

``measure_latency`` lives at ``benchmark.latency.run``; kind in
{touch, keyboard, back, scroll}.

Hermetic: stdlib unittest only, no network/secrets/services.
No device or input trace is required: missing-trace behavior is
asserted via LatencyError, numeric budgets via the returned report.

Expected state before implementation: RED — ``benchmark/latency/run.py``
does not exist, so every test fails (ModuleNotFoundError).
"""

import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LATENCY_RUN = os.path.join(REPO_ROOT, "benchmark", "latency", "run.py")

# ASSUMPTION (plan A4): threshold numbers verbatim from AC-PERFORMANCE-010.
TOUCH_MIN_MS = 1.0
TOUCH_MAX_MS = 2.0
KEYBOARD_MAX_MS = 16.0
BACK_MAX_MS = 1.0
SCROLL_MIN_FPS = 120.0


def import_latency_run():
    """Import the latency harness through its public boundary."""
    from benchmark.latency.run import LatencyError, measure_latency
    return LatencyError, measure_latency


class LatencyBudgetTest(unittest.TestCase):
    def test_touch_latency_within_bound(self):
        # AC-PERFORMANCE-010: touch 1-2 ms.
        _, measure_latency = import_latency_run()
        report = measure_latency("touch")
        self.assertGreaterEqual(report.ms, TOUCH_MIN_MS)
        self.assertLessEqual(report.ms, TOUCH_MAX_MS)

    def test_keyboard_latency_within_bound(self):
        # AC-PERFORMANCE-010: keyboard <16 ms.
        _, measure_latency = import_latency_run()
        report = measure_latency("keyboard")
        self.assertLess(report.ms, KEYBOARD_MAX_MS)

    def test_back_latency_within_bound(self):
        # AC-PERFORMANCE-010: back <1 ms.
        _, measure_latency = import_latency_run()
        report = measure_latency("back")
        self.assertLess(report.ms, BACK_MAX_MS)

    def test_scroll_fps_within_bound(self):
        # AC-PERFORMANCE-010: scroll >120 FPS on 10k-item feed.
        _, measure_latency = import_latency_run()
        report = measure_latency("scroll")
        self.assertGreater(report.fps, SCROLL_MIN_FPS)

    def test_report_exposes_ms_and_fps(self):
        _, measure_latency = import_latency_run()
        for kind in ("touch", "keyboard", "back", "scroll"):
            report = measure_latency(kind)
            self.assertGreaterEqual(report.ms, 0)
            self.assertGreaterEqual(report.fps, 0)

    def test_harness_module_exists(self):
        self.assertTrue(os.path.isfile(LATENCY_RUN))


class LatencyErrorTest(unittest.TestCase):
    def test_unknown_kind_raises_latency_error(self):
        LatencyError, measure_latency = import_latency_run()
        with self.assertRaises(LatencyError):
            measure_latency("pinch")

    def test_empty_kind_raises_latency_error(self):
        LatencyError, measure_latency = import_latency_run()
        with self.assertRaises(LatencyError):
            measure_latency("")

    def test_missing_trace_raises_latency_error(self):
        # Missing input trace must raise LatencyError, never crash.
        LatencyError, measure_latency = import_latency_run()
        with self.assertRaises(LatencyError):
            measure_latency("touch:missing-trace")

    def test_error_carries_kind_and_reason(self):
        LatencyError, measure_latency = import_latency_run()
        try:
            measure_latency("no-such-kind")
        except LatencyError as exc:
            self.assertEqual(exc.kind, "no-such-kind")
            self.assertTrue(exc.reason)
        else:
            self.fail("LatencyError not raised")

    def test_malicious_kind_rejected_fail_closed(self):
        # Injection / traversal payload must raise LatencyError, never crash.
        LatencyError, measure_latency = import_latency_run()
        with self.assertRaises(LatencyError):
            measure_latency("../../etc/passwd\x00touch")


if __name__ == "__main__":
    unittest.main()
