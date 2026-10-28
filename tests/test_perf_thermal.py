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


def import_thermal_tiers():
    """Import the tier-step signal through its public boundary."""
    from benchmark.thermal.tiers import tier_for_headroom
    return tier_for_headroom


# ASSUMPTION (plan T2 detail): tier ladder per GFX-010 Notes, ordered from
# the most degraded to the least degraded.
TIER_LADDER = ("low", "medium", "high", "ultra")


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


class TierStepTest(unittest.TestCase):
    """Batch 1 (T2) — thermal headroom -> quality-tier request signal.

    Covers NFR-PERFORMANCE-008 (AC-PERFORMANCE-017): frame time under
    sustained load via quality-tier degradation (perf-side request
    signal; tier switching itself stays in GFX-010, out of scope).

    Public boundary under test (plan Checklist T2)::

        tier_for_headroom(headroom_ms: float) -> Tier(
            name: str, degraded: bool)

    ``tier_for_headroom`` lives at ``benchmark.thermal.tiers`` and is a
    pure function: it never touches thermal control, the network, or the
    device, and it never drops frames itself.

    Expected state before implementation: RED —
    ``benchmark/thermal/tiers.py`` does not exist, so every test fails
    (ModuleNotFoundError).
    """

    def test_tier_module_exists(self):
        self.assertTrue(
            os.path.isfile(
                os.path.join(REPO_ROOT, "benchmark", "thermal", "tiers.py")))

    def test_returns_known_tier_name(self):
        tier_for_headroom = import_thermal_tiers()
        tier = tier_for_headroom(2.0)
        self.assertIn(tier.name, TIER_LADDER)
        self.assertIsInstance(tier.degraded, bool)

    def test_generous_headroom_is_not_degraded(self):
        # ASSUMPTION: ample headroom requests the top tier, not degraded.
        tier_for_headroom = import_thermal_tiers()
        tier = tier_for_headroom(1000.0)
        self.assertEqual(tier.name, "ultra")
        self.assertFalse(tier.degraded)

    def test_zero_headroom_is_most_degraded(self):
        # ASSUMPTION: exhausted headroom requests the bottom tier.
        tier_for_headroom = import_thermal_tiers()
        tier = tier_for_headroom(0.0)
        self.assertEqual(tier.name, "low")
        self.assertTrue(tier.degraded)

    def test_falling_headroom_steps_down_monotonically(self):
        # AC-PERFORMANCE-017: falling headroom steps tiers down, never up.
        tier_for_headroom = import_thermal_tiers()
        headrooms = [1000.0, 10.0, 5.0, 2.0, 1.0, 0.5, 0.0]
        ranks = [TIER_LADDER.index(tier_for_headroom(h).name)
                 for h in headrooms]
        for higher, lower in zip(ranks, ranks[1:]):
            self.assertLessEqual(lower, higher)

    def test_degraded_flag_monotonic_when_falling(self):
        # Once degraded, further falling headroom stays degraded.
        tier_for_headroom = import_thermal_tiers()
        headrooms = [1000.0, 10.0, 5.0, 2.0, 1.0, 0.5, 0.0]
        flags = [tier_for_headroom(h).degraded for h in headrooms]
        seen_true = False
        for flag in flags:
            if seen_true:
                self.assertTrue(flag)
            seen_true = seen_true or flag

    def test_pure_function_same_input_same_tier(self):
        tier_for_headroom = import_thermal_tiers()
        first = tier_for_headroom(2.0)
        second = tier_for_headroom(2.0)
        self.assertEqual(first, second)

    def test_pure_function_ignores_thermal_control_env(self):
        # The tier signal never touches thermal control: unavailable
        # control must not raise here (fail-closed only applies to the
        # measurement probes, not to this pure signal).
        tier_for_headroom = import_thermal_tiers()
        old = os.environ.get("MUSE_THERMAL_CONTROL")
        os.environ["MUSE_THERMAL_CONTROL"] = "unavailable"
        try:
            tier = tier_for_headroom(2.0)
        finally:
            if old is None:
                del os.environ["MUSE_THERMAL_CONTROL"]
            else:
                os.environ["MUSE_THERMAL_CONTROL"] = old
        self.assertIn(tier.name, TIER_LADDER)


if __name__ == "__main__":
    unittest.main()
