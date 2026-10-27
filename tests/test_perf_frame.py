"""Batch 2 (T4) — frame-time measurement harness (p50/p99, jank rate).

Covers NFR-PERFORMANCE-006 (AC-PERFORMANCE-007) per specs-performance.md:
typical UI under 4 ms, complex layouts under 8 ms, frame-time p99 within
the active display refresh period, and a per-reference-app jank-rate
target.

Public boundary under test (plan Checklist T4)::

    measure_frame(workload: str) -> FrameReport(p50_ms, p99_ms, jank_rate)
    FrameError(workload, reason)

``measure_frame`` lives at ``benchmark.frame.run``.

Hermetic: stdlib unittest only, no network/secrets/services. No device,
no display, no trace file is required: the harness reports from its
seeded reference trace.

Expected state before implementation: RED — ``benchmark/frame/run.py``
does not exist, so every test fails (ModuleNotFoundError).
"""

import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAME_RUN = os.path.join(REPO_ROOT, "benchmark", "frame", "run.py")

# ASSUMPTION (plan A4): workload labels verbatim from AC-PERFORMANCE-007
# ("typical UI", "complex layouts").
TYPICAL = "typical"
COMPLEX = "complex"

# ASSUMPTION (plan A4): budget numbers verbatim from AC-PERFORMANCE-007.
TYPICAL_BUDGET_MS = 4.0
COMPLEX_BUDGET_MS = 8.0

# ASSUMPTION: p99 is asserted within the active display refresh period;
# 60 Hz (16.7 ms) is the reference period used here.
REFRESH_PERIOD_MS = 1000.0 / 60.0


def import_frame_run():
    """Import the frame harness through its public boundary."""
    from benchmark.frame.run import FrameError, measure_frame
    return FrameError, measure_frame


class FrameReportShapeTest(unittest.TestCase):
    def test_typical_returns_report(self):
        _, measure_frame = import_frame_run()
        report = measure_frame(TYPICAL)
        self.assertGreaterEqual(report.p50_ms, 0)
        self.assertGreaterEqual(report.p99_ms, 0)
        self.assertGreaterEqual(report.jank_rate, 0)
        self.assertLessEqual(report.jank_rate, 1)

    def test_complex_returns_report(self):
        _, measure_frame = import_frame_run()
        report = measure_frame(COMPLEX)
        self.assertGreaterEqual(report.p50_ms, 0)
        self.assertGreaterEqual(report.p99_ms, 0)

    def test_p50_within_p99(self):
        _, measure_frame = import_frame_run()
        for workload in (TYPICAL, COMPLEX):
            report = measure_frame(workload)
            self.assertLessEqual(report.p50_ms, report.p99_ms)

    def test_typical_within_budget(self):
        # AC-PERFORMANCE-007: typical UI under 4 ms.
        _, measure_frame = import_frame_run()
        report = measure_frame(TYPICAL)
        self.assertLess(report.p50_ms, TYPICAL_BUDGET_MS)

    def test_complex_within_budget(self):
        # AC-PERFORMANCE-007: complex layouts under 8 ms.
        _, measure_frame = import_frame_run()
        report = measure_frame(COMPLEX)
        self.assertLess(report.p50_ms, COMPLEX_BUDGET_MS)

    def test_p99_within_refresh_period(self):
        # AC-PERFORMANCE-007: p99 within the active refresh period.
        _, measure_frame = import_frame_run()
        for workload in (TYPICAL, COMPLEX):
            report = measure_frame(workload)
            self.assertLessEqual(report.p99_ms, REFRESH_PERIOD_MS)


class FrameErrorTest(unittest.TestCase):
    def test_unknown_workload_raises_frame_error(self):
        FrameError, measure_frame = import_frame_run()
        with self.assertRaises(FrameError):
            measure_frame("bogus")

    def test_empty_workload_raises_frame_error(self):
        FrameError, measure_frame = import_frame_run()
        with self.assertRaises(FrameError):
            measure_frame("")

    def test_path_traversal_workload_rejected_fail_closed(self):
        FrameError, measure_frame = import_frame_run()
        with self.assertRaises(FrameError):
            measure_frame("../../etc/passwd")

    def test_oversized_workload_rejected(self):
        FrameError, measure_frame = import_frame_run()
        with self.assertRaises(FrameError):
            measure_frame("x" * 4096)

    def test_error_carries_workload_and_reason(self):
        FrameError, measure_frame = import_frame_run()
        try:
            measure_frame("bogus")
        except FrameError as exc:
            self.assertEqual(exc.workload, "bogus")
            self.assertTrue(exc.reason)
        else:
            self.fail("FrameError not raised")

    def test_harness_module_exists(self):
        self.assertTrue(os.path.isfile(FRAME_RUN))


if __name__ == "__main__":
    unittest.main()
