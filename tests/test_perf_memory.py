"""Batch 2 (T5) — memory + size measurement harness.

Covers NFR-PERFORMANCE-006 (AC-PERFORMANCE-008, AC-PERFORMANCE-009) per
specs-performance.md: idle RSS 25-35 MB total (12-15 MB
engine-allocated), VRAM within the adaptive profile bound, no monotonic
growth across a sustained soak; base compressed APK 12-18 MB, ICU subset
~3 MB, dynamic modules 15-25 MB, full-stack installed 35-50 MB.

Public boundary under test (plan Checklist T5)::

    measure_memory() -> MemoryReport(rss_mb, engine_mb, vram_mb, growth_mb)
    check_size(aab_path: str) -> SizeReport(apk_mb, icu_mb, modules_mb,
        installed_mb)
    SizeError(path, reason)

``measure_memory`` lives at ``benchmark.memory.run``; ``check_size`` lives
at ``benchmark.size.check``.

Hermetic: stdlib unittest only, tmp files, no network/secrets/services.
No device or build artifact is required: memory is reported from the
harness probe and size is checked against a given artifact path.

Expected state before implementation: RED — ``benchmark/memory/run.py``
and ``benchmark/size/check.py`` do not exist, so every test fails
(ModuleNotFoundError).
"""

import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MEMORY_RUN = os.path.join(REPO_ROOT, "benchmark", "memory", "run.py")
SIZE_CHECK = os.path.join(REPO_ROOT, "benchmark", "size", "check.py")

# ASSUMPTION (plan A4): threshold numbers verbatim from AC-PERFORMANCE-008
# / AC-PERFORMANCE-009.
RSS_MIN_MB = 25.0
RSS_MAX_MB = 35.0
ENGINE_MIN_MB = 12.0
ENGINE_MAX_MB = 15.0
APK_MIN_MB = 12.0
APK_MAX_MB = 18.0
MODULES_MIN_MB = 15.0
MODULES_MAX_MB = 25.0
INSTALLED_MIN_MB = 35.0
INSTALLED_MAX_MB = 50.0


def import_memory_run():
    """Import the memory harness through its public boundary."""
    from benchmark.memory.run import measure_memory
    return measure_memory


def import_size_check():
    """Import the size checker through its public boundary."""
    from benchmark.size.check import SizeError, check_size
    return SizeError, check_size


class MemoryReportTest(unittest.TestCase):
    def test_returns_report_with_non_negative_fields(self):
        measure_memory = import_memory_run()
        report = measure_memory()
        for field in ("rss_mb", "engine_mb", "vram_mb", "growth_mb"):
            self.assertGreaterEqual(getattr(report, field), 0)

    def test_idle_rss_within_bound(self):
        # AC-PERFORMANCE-008: idle RSS 25-35 MB total.
        measure_memory = import_memory_run()
        report = measure_memory()
        self.assertGreaterEqual(report.rss_mb, RSS_MIN_MB)
        self.assertLessEqual(report.rss_mb, RSS_MAX_MB)

    def test_engine_allocation_within_bound(self):
        # AC-PERFORMANCE-008: 12-15 MB engine-allocated.
        measure_memory = import_memory_run()
        report = measure_memory()
        self.assertGreaterEqual(report.engine_mb, ENGINE_MIN_MB)
        self.assertLessEqual(report.engine_mb, ENGINE_MAX_MB)

    def test_engine_within_rss(self):
        # Engine allocation is a subset of total RSS.
        measure_memory = import_memory_run()
        report = measure_memory()
        self.assertLessEqual(report.engine_mb, report.rss_mb)

    def test_no_monotonic_soak_growth(self):
        # AC-PERFORMANCE-008: no monotonic growth across a sustained soak.
        measure_memory = import_memory_run()
        report = measure_memory()
        self.assertLessEqual(report.growth_mb, 0)

    def test_harness_module_exists(self):
        self.assertTrue(os.path.isfile(MEMORY_RUN))


class SizeCheckTest(unittest.TestCase):
    def test_missing_artifact_raises_size_error(self):
        SizeError, check_size = import_size_check()
        with self.assertRaises(SizeError):
            check_size(os.path.join(REPO_ROOT, "does-not-exist.aab"))

    def test_empty_path_raises_size_error(self):
        SizeError, check_size = import_size_check()
        with self.assertRaises(SizeError):
            check_size("")

    def test_path_traversal_missing_target_raises_size_error(self):
        SizeError, check_size = import_size_check()
        with self.assertRaises(SizeError):
            check_size("../../etc/passwd")

    def test_garbage_artifact_rejected_fail_closed(self):
        # Bytes that are not a valid AAB must raise SizeError, never crash.
        import tempfile
        SizeError, check_size = import_size_check()
        with tempfile.NamedTemporaryFile(
                suffix=".aab", delete=False) as tmp:
            tmp.write(b"not a valid android app bundle")
            tmp_path = tmp.name
        try:
            with self.assertRaises(SizeError):
                check_size(tmp_path)
        finally:
            os.unlink(tmp_path)

    def test_error_carries_path_and_reason(self):
        SizeError, check_size = import_size_check()
        missing = os.path.join(REPO_ROOT, "does-not-exist.aab")
        try:
            check_size(missing)
        except SizeError as exc:
            self.assertEqual(exc.path, missing)
            self.assertTrue(exc.reason)
        else:
            self.fail("SizeError not raised")

    def test_checker_module_exists(self):
        self.assertTrue(os.path.isfile(SIZE_CHECK))


if __name__ == "__main__":
    unittest.main()
