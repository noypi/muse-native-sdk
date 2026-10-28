"""Batch 3 (T7) — thermal/battery/network probe harness.

Covers NFR-PERFORMANCE-006 (AC-PERFORMANCE-011): battery budget row
checked, HTTP dispatch <1 ms, sustained frame time held.

Public boundary under test (plan Checklist T7)::

    measure_sustained() -> ThermalReport(p99_ms: float, drain_mah: float,
        http_dispatch_ms: float, headroom: float)
    ThermalError(reason: str)

``measure_sustained`` lives at ``benchmark.thermal.run``.

Hermetic: stdlib unittest only, no network/secrets/services.
No device or thermal control is required: unavailable-control behavior
is asserted via ThermalError shape, numeric budgets via the report.

Expected state before implementation: RED — ``benchmark/thermal/run.py``
does not exist, so every test fails (ModuleNotFoundError).
"""

import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THERMAL_RUN = os.path.join(REPO_ROOT, "benchmark", "thermal", "run.py")

# ASSUMPTION (plan A4): threshold numbers verbatim from AC-PERFORMANCE-011.
HTTP_DISPATCH_MAX_MS = 1.0
# ASSUMPTION: "sustained frame held" means sustained p99 stays within the
# complex-frame budget (8 ms) from AC-PERFORMANCE-007.
SUSTAINED_P99_MAX_MS = 8.0


def import_thermal_run():
    """Import the thermal harness through its public boundary."""
    from benchmark.thermal.run import ThermalError, measure_sustained
    return ThermalError, measure_sustained


class ThermalReportTest(unittest.TestCase):
    def test_returns_report_with_non_negative_fields(self):
        _, measure_sustained = import_thermal_run()
        report = measure_sustained()
        for field in ("p99_ms", "drain_mah", "http_dispatch_ms", "headroom"):
            self.assertGreaterEqual(getattr(report, field), 0)

    def test_http_dispatch_within_bound(self):
        # AC-PERFORMANCE-011: HTTP dispatch <1 ms.
        _, measure_sustained = import_thermal_run()
        report = measure_sustained()
        self.assertLess(report.http_dispatch_ms, HTTP_DISPATCH_MAX_MS)

    def test_sustained_frame_held(self):
        # AC-PERFORMANCE-011: sustained frame time held.
        _, measure_sustained = import_thermal_run()
        report = measure_sustained()
        self.assertLessEqual(report.p99_ms, SUSTAINED_P99_MAX_MS)

    def test_battery_row_checked(self):
        # AC-PERFORMANCE-011: battery budget row checked — drain recorded.
        _, measure_sustained = import_thermal_run()
        report = measure_sustained()
        self.assertGreaterEqual(report.drain_mah, 0)
        self.assertTrue(hasattr(report, "drain_mah"))

    def test_headroom_recorded(self):
        # Falling-headroom probe must record remaining headroom.
        _, measure_sustained = import_thermal_run()
        report = measure_sustained()
        self.assertGreaterEqual(report.headroom, 0)

    def test_harness_module_exists(self):
        self.assertTrue(os.path.isfile(THERMAL_RUN))


class ThermalErrorTest(unittest.TestCase):
    def test_error_carries_reason(self):
        # ThermalError(reason) shape — constructible with a reason.
        ThermalError, _ = import_thermal_run()
        exc = ThermalError("thermal control unavailable")
        self.assertTrue(exc.reason)
        self.assertEqual(str(exc.reason), "thermal control unavailable")

    def test_error_is_exception(self):
        ThermalError, _ = import_thermal_run()
        self.assertTrue(issubclass(ThermalError, Exception))

    def test_error_message_does_not_leak_secrets(self):
        # Error text must not embed secrets or internals.
        ThermalError, _ = import_thermal_run()
        exc = ThermalError("thermal control unavailable")
        text = str(exc).lower()
        self.assertNotIn("api_key", text)
        self.assertNotIn("token", text)


if __name__ == "__main__":
    unittest.main()
