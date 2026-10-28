"""Batch 2 (T3) — honest GC/driver/OS overhead harness + doc note.

Covers NFR-PERFORMANCE-010 (AC-PERFORMANCE-023, AC-PERFORMANCE-024):
honest GC/driver/OS modeling with the CORE-008 bound reproduced as an
explicit target, never as a measured result.

Public boundary under test (plan Checklist T3 Interface line,
reproduced verbatim)::

    measure_js_gc() -> JsGcReport(p99_ms: float, over_budget_frames: int, status: str)

where ``status`` is ``"target"`` until a device-lab datum exists; doc
section headings fixed: ``render-path split``, ``js-gc bound``,
``driver bound``, ``os bound``, ``no-absolute-claim``.

``measure_js_gc`` lives at ``benchmark/js-gc/run.py`` (hyphenated path,
loaded by file location so the test does not depend on package naming).
The doc under test is ``docs/performance/gc-driver-os.md`` (created by
the coder in T3).

Hermetic: stdlib unittest only, no network/secrets/services/config.

Expected state before T3 implementation: RED — ``benchmark/js-gc/run.py``
and ``docs/performance/gc-driver-os.md`` do not exist, so the module/doc
existence tests and every behavioral test fail (missing file or
FileNotFoundError/ImportError). Tests that assert the shared zero-claim
scanner stays clean pass from the start on tmp fixtures (scanner only).
"""

import importlib.util
import os
import re
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JS_GC_RUN = os.path.join(REPO_ROOT, "benchmark", "js-gc", "run.py")
DOC_PATH = os.path.join(REPO_ROOT, "docs", "performance", "gc-driver-os.md")
DOCS_PERF_ROOT = os.path.join(REPO_ROOT, "docs", "performance")

# ASSUMPTION (plan A4 + AC-CORE-018): the CORE-008 GC bound reproduced as
# an explicit target is p99 < 2 ms with no single-GC frame over budget.
JS_GC_P99_TARGET_MS = 2.0

REQUIRED_HEADINGS = (
    "render-path split",
    "js-gc bound",
    "driver bound",
    "os bound",
    "no-absolute-claim",
)


def load_js_gc_module():
    """Load the js-gc harness through its public boundary (by file path)."""
    spec = importlib.util.spec_from_file_location("js_gc_run", JS_GC_RUN)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_zero_claim_scanner():
    """Load scan_zero_claims from the sibling T4/Batch-3 scanner test."""
    path = os.path.join(REPO_ROOT, "tests", "test_perf_zero_claims.py")
    spec = importlib.util.spec_from_file_location("zero_claims_scan", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.scan_zero_claims


class HarnessModuleTest(unittest.TestCase):
    def test_harness_module_exists(self):
        self.assertTrue(
            os.path.isfile(JS_GC_RUN),
            msg="benchmark/js-gc/run.py must exist (created by T3)",
        )

    def test_exposes_measure_js_gc_callable(self):
        module = load_js_gc_module()
        self.assertTrue(callable(getattr(module, "measure_js_gc", None)))

    def test_exposes_js_gc_report_type(self):
        module = load_js_gc_module()
        self.assertTrue(hasattr(module, "JsGcReport"))


class JsGcReportTest(unittest.TestCase):
    def test_returns_report_with_required_fields(self):
        module = load_js_gc_module()
        report = module.measure_js_gc()
        for field in ("p99_ms", "over_budget_frames", "status"):
            self.assertTrue(
                hasattr(report, field), msg="JsGcReport missing %s" % field
            )

    def test_p99_within_target_bound(self):
        # AC-CORE-018 reproduced as TARGET: p99 < 2 ms.
        module = load_js_gc_module()
        report = module.measure_js_gc()
        self.assertLess(report.p99_ms, JS_GC_P99_TARGET_MS)

    def test_no_single_gc_frame_over_budget(self):
        # AC-CORE-018: no frame over budget attributable to a single GC.
        module = load_js_gc_module()
        report = module.measure_js_gc()
        self.assertEqual(report.over_budget_frames, 0)

    def test_status_is_target_until_device_datum(self):
        # Plan Interface: status is "target" until a device-lab datum exists.
        module = load_js_gc_module()
        report = module.measure_js_gc()
        self.assertEqual(report.status, "target")

    def test_p99_and_over_budget_are_numeric(self):
        module = load_js_gc_module()
        report = module.measure_js_gc()
        self.assertGreaterEqual(report.p99_ms, 0)
        self.assertGreaterEqual(report.over_budget_frames, 0)
        self.assertIsInstance(report.over_budget_frames, int)


class HonestDocTest(unittest.TestCase):
    def test_doc_exists(self):
        self.assertTrue(
            os.path.isfile(DOC_PATH),
            msg="docs/performance/gc-driver-os.md must exist (created by T3)",
        )

    def test_doc_has_all_fixed_section_headings(self):
        with open(DOC_PATH, encoding="utf-8") as fh:
            text = fh.read().lower()
        for heading in REQUIRED_HEADINGS:
            self.assertIn(
                heading, text, msg="doc missing fixed heading %r" % heading
            )

    def test_doc_states_render_path_gc_free_split(self):
        # TC-023 grep `render-path GC-free` must hit the new doc.
        with open(DOC_PATH, encoding="utf-8") as fh:
            text = fh.read()
        self.assertIn("render-path GC-free", text)

    def test_doc_names_js_gc_bound_or_explicit_tbd(self):
        # TC-024 manual checklist: JS GC names a bound or explicit TBD.
        with open(DOC_PATH, encoding="utf-8") as fh:
            text = fh.read()
        self.assertTrue(
            ("2 ms" in text or "2ms" in text or "TBD" in text),
            msg="doc must name the JS GC bound or an explicit TBD",
        )

    def test_doc_names_driver_bound_or_explicit_tbd(self):
        with open(DOC_PATH, encoding="utf-8") as fh:
            text = fh.read().lower()
        self.assertTrue(
            ("driver" in text and ("tbd" in text or "bound" in text)),
            msg="doc must name the driver bound or an explicit TBD",
        )

    def test_doc_names_os_bound_or_explicit_tbd(self):
        with open(DOC_PATH, encoding="utf-8") as fh:
            text = fh.read().lower()
        self.assertTrue(
            ("os" in text and ("tbd" in text or "bound" in text)),
            msg="doc must name the OS bound or an explicit TBD",
        )

    def test_doc_disclaims_absolute_claim(self):
        # AC-PERFORMANCE-025: no absolute process-wide zero-GC claim.
        with open(DOC_PATH, encoding="utf-8") as fh:
            text = fh.read().lower()
        self.assertTrue(
            ("no " in text or "not " in text or "never " in text)
            and "zero" in text,
            msg="doc must carry an explicit no-absolute-claim denial",
        )


class ZeroClaimRegressionTest(unittest.TestCase):
    def test_new_doc_passes_zero_claim_scan(self):
        # T4 gate: the expanded docs tree must stay scan-clean.
        scan_zero_claims = load_zero_claim_scanner()
        with open(DOC_PATH, encoding="utf-8") as fh:
            doc_text = fh.read()
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "gc-driver-os.md")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(doc_text)
            self.assertEqual(scan_zero_claims(tmp), [])

    def test_full_docs_tree_stays_scan_clean(self):
        # T4 acceptance: scan over docs/performance finds no violations.
        scan_zero_claims = load_zero_claim_scanner()
        self.assertEqual(scan_zero_claims(DOCS_PERF_ROOT), [])


if __name__ == "__main__":
    unittest.main()
